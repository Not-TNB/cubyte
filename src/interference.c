#include "../include/interference.h"

#include "../include/piece.h"
#include "../include/util.h"

#include <stdlib.h>
#include <string.h>

#define IG_INITIAL_CAPACITY 8
#define LIVESET_BITS 64

static LiveSet live_bit(int index) {
    if (index < 0 || index >= LIVESET_BITS) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
            "variable index %d does not fit in liveness bitset", index);
    }

    return UINT64_C(1) << index;
}

static bool is_r0_index(TypeEnv *env, int index) {
    return index == typeenv_index(env, R0_SYNTHETIC_VARIABLE);
}

static bool ig_var_allowed(TypeEnv *env, int index) {
    if (env == NULL || index < 0 || index >= env->count) {
        return false;
    }

    return env->entries[index].type == TYPE_INT && !is_r0_index(env, index);
}

static void check_liveness_indices(TypeEnv *env) {
    for (int i = 0; i < env->count; i++) {
        if (ig_var_allowed(env, i) && i >= LIVESET_BITS) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
                "int variable '%s' has index %d outside liveness bitset",
                env->entries[i].name, i);
        }
    }
}

static void ig_init(InterferenceGraph *ig, TypeEnv *env) {
    if (ig == NULL || env == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "null interference graph");
    }

    ig->nodes = malloc(sizeof(IGNode) * IG_INITIAL_CAPACITY);
    if (ig->nodes == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    ig->count = 0;
    ig->capacity = IG_INITIAL_CAPACITY;
    ig->env = env;
}

static void ig_add_node(InterferenceGraph *ig, int var_index) {
    if (!ig_var_allowed(ig->env, var_index) ||
        ig_find_node(ig, var_index) != NULL) {
        return;
    }

    if (ig->count == ig->capacity) {
        int new_capacity = ig->capacity * 2;
        IGNode *new_nodes = realloc(ig->nodes,
                                    sizeof(IGNode) * new_capacity);
        if (new_nodes == NULL) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "realloc failed");
        }

        ig->nodes = new_nodes;
        ig->capacity = new_capacity;
    }

    IGNode *node = &ig->nodes[ig->count++];
    node->var_index     = var_index;
    node->name          = ig->env->entries[var_index].name;
    node->neighbours    = 0;
    node->required_order = ig->env->entries[var_index].required_order;
}

InterferenceGraph *ig_build(TypeEnv *env, const IGLivenessNode *nodes,
                            int node_count) {
    if (env == NULL || (nodes == NULL && node_count > 0) || node_count < 0) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "bad ig_build input");
    }

    InterferenceGraph *ig = malloc(sizeof(InterferenceGraph));
    if (ig == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    ig_init(ig, env);
    check_liveness_indices(env);

    for (int i = 0; i < env->count; i++) {
        ig_add_node(ig, i);
    }

    for (int i = 0; i < node_count; i++) {
        LiveSet defs = nodes[i].def;
        for (int x = 0; x < env->count && x < LIVESET_BITS; x++) {
            if ((defs & live_bit(x)) == 0 || !ig_var_allowed(env, x)) {
                continue;
            }

            LiveSet live_out = nodes[i].live_out & ~live_bit(x);
            for (int y = 0; y < env->count && y < LIVESET_BITS; y++) {
                if ((live_out & live_bit(y)) != 0) {
                    ig_add_edge(ig, x, y);
                }
            }
        }
    }

    return ig;
}

void ig_free(InterferenceGraph *ig) {
    if (ig == NULL) {
        return;
    }

    free(ig->nodes);
    free(ig);
}

IGNode *ig_find_node(InterferenceGraph *ig, int var_index) {
    if (ig == NULL) {
        return NULL;
    }

    for (int i = 0; i < ig->count; i++) {
        if (ig->nodes[i].var_index == var_index) {
            return &ig->nodes[i];
        }
    }

    return NULL;
}

void ig_add_edge(InterferenceGraph *ig, int a, int b) {
    if (ig == NULL || a == b || !ig_var_allowed(ig->env, a) ||
        !ig_var_allowed(ig->env, b)) {
        return;
    }

    IGNode *node_a = ig_find_node(ig, a);
    IGNode *node_b = ig_find_node(ig, b);
    if (node_a == NULL || node_b == NULL) {
        return;
    }

    node_a->neighbours |= live_bit(b);
    node_b->neighbours |= live_bit(a);
}

bool ig_interfere(InterferenceGraph *ig, int a, int b) {
    IGNode *node = ig_find_node(ig, a);
    if (node == NULL || b < 0 || b >= LIVESET_BITS) {
        return false;
    }

    return (node->neighbours & live_bit(b)) != 0;
}

static int compare_node_names(const void *a, const void *b) {
    const IGNode *node_a = *(const IGNode *const *)a;
    const IGNode *node_b = *(const IGNode *const *)b;
    return strcmp(node_a->name, node_b->name);
}

static IGNode **sorted_node_refs(InterferenceGraph *ig) {
    IGNode **refs = malloc(sizeof(IGNode *) * (size_t)ig->count);
    if (refs == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    for (int i = 0; i < ig->count; i++) {
        refs[i] = &ig->nodes[i];
    }

    qsort(refs, (size_t)ig->count, sizeof(IGNode *), compare_node_names);
    return refs;
}

void ig_dump(InterferenceGraph *ig, FILE *out) {
    if (ig == NULL || out == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "null ig dump");
    }

    if (ig->count == 0) {
        return;
    }

    IGNode **nodes = sorted_node_refs(ig);
    for (int i = 0; i < ig->count; i++) {
        IGNode *node = nodes[i];
        fprintf(out, "%s:", node->name);

        for (int j = 0; j < ig->count; j++) {
            IGNode *neighbour = nodes[j];
            if ((node->neighbours & live_bit(neighbour->var_index)) != 0) {
                fprintf(out, " %s", neighbour->name);
            }
        }

        fputc('\n', out);
    }

    free(nodes);
}
