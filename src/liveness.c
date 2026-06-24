#include "../include/liveness.h"

#include "../include/piece.h"
#include "../include/util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIVESET_BITS 64

/*
  This file owns two closely related stages:

  1. CFG construction.
     The AST is recursive because if/while statements contain nested statement
     arrays. Liveness is easier on a flat graph, so cfg_build turns the whole
     program into numbered CFGNode objects with successor edges.

  2. Liveness analysis.
     Once the graph is flat, each node gets four bitsets:
       use      variables read by this node before being written here
       def      variables written by this node
       live_in  variables needed before this node runs
       live_out variables needed after this node runs

     The interference graph only needs def and live_out, so LivenessResult also
     builds the compact IGLivenessNode array consumed by ig_build().
 */

/*
  Tiny growable int list used while building structured control flow.
  In particular, a block can have multiple "open exits": places that should
  fall through to whatever statement comes next once that next statement exists.
 */
typedef struct {
    int *items;
    int count;
    int capacity;
} IntList;

/*
  Result of building a statement or statement list.

  first is the first CFG node in the subgraph, or -1 for an empty list.
  exits contains CFG nodes that should fall through to the following statement.

  Example:
    if c { a } else { b }

  first is the condition node. exits contains the tail of the then branch and
  the tail of the else branch, because both continue after the if statement.
 */
typedef struct {
    int first;
    IntList exits;
} BuildResult;

/* Label names are borrowed from the AST. The CFG does not own these strings. */
typedef struct {
    char *name;
    int node;
} LabelTarget;

/*
  Gotos can target labels that appear later in the program, so we record them
  during the recursive walk and resolve them after every label node is known.
 */
typedef struct {
    int node;
    char *target;
} PendingGoto;

/* Mutable state shared by the recursive CFG builder helpers. */
typedef struct {
    CFG *cfg;

    LabelTarget *labels;
    int label_count;
    int label_capacity;

    PendingGoto *gotos;
    int goto_count;
    int goto_capacity;
} CFGBuilder;

/* Append to a growable int list. Used for exit lists, not program data. */
static void intlist_push(IntList *list, int value) {
    if (list->count == list->capacity) {
        int new_capacity = next_cap(list->capacity);
        int *new_items = realloc(list->items, sizeof(int) * (size_t)new_capacity);
        if (new_items == NULL) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "realloc failed");
        }

        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count++] = value;
}

/* Copy all values from src into dst, preserving order. */
static void intlist_extend(IntList *dst, const IntList *src) {
    for (int i = 0; i < src->count; i++) {
        intlist_push(dst, src->items[i]);
    }
}

/* Release list storage and reset it so accidental reuse is obvious. */
static void intlist_free(IntList *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/*
  Convert a TypeEnv index into a LiveSet bit.

  This implementation deliberately uses a 64-bit bitset because the language is
  small and the spec allows a hard cap. Alg variables are compile-time only, but
  int variables, temporaries, and R0 all still need valid indices.
 */
static LiveSet live_bit(int index) {
    if (index < 0 || index >= LIVESET_BITS) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
            "variable index %d does not fit in liveness bitset", index);
    }

    return UINT64_C(1) << index;
}

/* Allocate an empty CFG. Entry and exit nodes are added by cfg_build(). */
static CFG *cfg_new(void) {
    CFG *cfg = malloc(sizeof(CFG));
    if (cfg == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    cfg->nodes = NULL;
    cfg->count = 0;
    cfg->capacity = 0;
    cfg->entry = -1;
    cfg->exit = -1;
    return cfg;
}

/*
  Add one node to the flat CFG.

  The Statement pointer is borrowed from the AST. Entry and exit nodes have
  stmt == NULL. All liveness bitsets start empty and are filled later by
  liveness_analyze().
 */
static int cfg_add_node(CFG *cfg, CFGNodeKind kind, Statement *stmt) {
    if (cfg->count == cfg->capacity) {
        int new_capacity = next_cap(cfg->capacity);
        CFGNode *new_nodes =
            realloc(cfg->nodes, sizeof(CFGNode) * (size_t)new_capacity);
        if (new_nodes == NULL) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "realloc failed");
        }

        cfg->nodes = new_nodes;
        cfg->capacity = new_capacity;
    }

    int id = cfg->count;
    CFGNode *node = &cfg->nodes[cfg->count++];
    node->id = id;
    node->kind = kind;
    node->stmt = stmt;
    node->succ_count = 0;
    node->use = 0;
    node->def = 0;
    node->live_in = 0;
    node->live_out = 0;
    return id;
}

/*
  Add a directed edge, avoiding duplicates.

  CuBit CFG nodes need at most two successors:
    ordinary statements: one fallthrough edge
    if/while conditions: true and false edges
    goto: one jump edge
 */
static void cfg_add_edge(CFG *cfg, int from, int to) {
    if (from < 0 || to < 0 || from >= cfg->count || to >= cfg->count) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
            "bad cfg edge %d -> %d", from, to);
    }

    CFGNode *node = &cfg->nodes[from];
    for (int i = 0; i < node->succ_count; i++) {
        if (node->succ[i] == to) {
            return;
        }
    }

    if (node->succ_count == 2) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
            "cfg node %d has too many successors", from);
    }

    node->succ[node->succ_count++] = to;
}

/*
  Connect all currently open fallthrough exits to target.

  This is the small trick that lets the recursive builder handle nested control
  flow without knowing the "next" statement until it has been built.
 */
static void patch_exits(CFGBuilder *builder, const IntList *exits, int target) {
    for (int i = 0; i < exits->count; i++) {
        cfg_add_edge(builder->cfg, exits->items[i], target);
    }
}

/* Remember that a label name maps to a specific CFG node. */
static void labels_push(CFGBuilder *builder, char *name, int node) {
    for (int i = 0; i < builder->label_count; i++) {
        if (builder->labels[i].name != NULL && name != NULL &&
            strcmp(builder->labels[i].name, name) == 0) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
                "duplicate label node '%s'", name);
        }
    }

    if (builder->label_count == builder->label_capacity) {
        int new_capacity = next_cap(builder->label_capacity);
        LabelTarget *new_labels =
            realloc(builder->labels, sizeof(LabelTarget) * (size_t)new_capacity);
        if (new_labels == NULL) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "realloc failed");
        }

        builder->labels = new_labels;
        builder->label_capacity = new_capacity;
    }

    builder->labels[builder->label_count].name = name;
    builder->labels[builder->label_count].node = node;
    builder->label_count++;
}

/* Record a goto edge that will be resolved after the full label map exists. */
static void gotos_push(CFGBuilder *builder, int node, char *target) {
    if (builder->goto_count == builder->goto_capacity) {
        int new_capacity = next_cap(builder->goto_capacity);
        PendingGoto *new_gotos =
            realloc(builder->gotos, sizeof(PendingGoto) * (size_t)new_capacity);
        if (new_gotos == NULL) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "realloc failed");
        }

        builder->gotos = new_gotos;
        builder->goto_capacity = new_capacity;
    }

    builder->gotos[builder->goto_count].node = node;
    builder->gotos[builder->goto_count].target = target;
    builder->goto_count++;
}

/* Return the CFG node for a label target, or -1 if it was not collected. */
static int label_lookup(CFGBuilder *builder, const char *target) {
    for (int i = 0; i < builder->label_count; i++) {
        if (builder->labels[i].name != NULL && target != NULL &&
            strcmp(builder->labels[i].name, target) == 0) {
            return builder->labels[i].node;
        }
    }

    return -1;
}

static BuildResult build_statements(CFGBuilder *builder,
                                    Statement **statements, int count);

/* A normal statement has itself as its first node and one fallthrough exit. */
static BuildResult single_exit(int node) {
    BuildResult result;
    result.first = node;
    result.exits = (IntList){0};
    intlist_push(&result.exits, node);
    return result;
}

/* A goto has a first node, but no fallthrough exit. */
static BuildResult no_exit(int node) {
    BuildResult result;
    result.first = node;
    result.exits = (IntList){0};
    return result;
}

/*
  Build the CFG subgraph for one AST statement.

  For structured statements, the condition/header node is represented by the
  original STMT_IF or STMT_WHILE. Later liveness uses that same statement node
  to collect condition variable uses.
 */
static BuildResult build_statement(CFGBuilder *builder, Statement *stmt) {
    if (stmt == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "null statement in cfg");
    }

    switch (stmt->kind) {
    case STMT_IF: {
        int cond = cfg_add_node(builder->cfg, CFG_NODE_STMT, stmt);
        BuildResult result = {cond, {0}};

        /*
          True branch. If the body is empty, the condition itself remains an
          open exit to represent "condition true, then immediately fall through".
         */
        BuildResult then_result = build_statement(builder, stmt->if_stmt.if_then);
        if (then_result.first >= 0) {
            cfg_add_edge(builder->cfg, cond, then_result.first);
            intlist_extend(&result.exits, &then_result.exits);
        } else {
            intlist_push(&result.exits, cond);
        }
        intlist_free(&then_result.exits);

        /*
          False branch. if_else is NULL when there is no else clause, which
          naturally becomes a fallthrough exit from the condition node.
         */
        if (stmt->if_stmt.if_else != NULL) {
            BuildResult else_result = build_statement(builder, stmt->if_stmt.if_else);
            if (else_result.first >= 0) {
                cfg_add_edge(builder->cfg, cond, else_result.first);
                intlist_extend(&result.exits, &else_result.exits);
            } else {
                intlist_push(&result.exits, cond);
            }
            intlist_free(&else_result.exits);
        } else {
            intlist_push(&result.exits, cond);
        }

        return result;
    }

    case STMT_WHILE: {
        int cond = cfg_add_node(builder->cfg, CFG_NODE_STMT, stmt);
        BuildResult body = build_statement(builder, stmt->while_stmt.while_body);

        /*
          The true edge enters the body and every normal body exit jumps back to
          the condition. The false edge is represented by returning cond as an
          open exit, so the surrounding statement list will patch it to whatever
          follows the loop.
         */
        if (body.first >= 0) {
            cfg_add_edge(builder->cfg, cond, body.first);
            patch_exits(builder, &body.exits, cond);
        } else {
            /*
              Empty while bodies are still loops. The true edge goes straight
              back to the condition; the false edge is patched by the caller.
             */
            cfg_add_edge(builder->cfg, cond, cond);
        }
        intlist_free(&body.exits);

        return single_exit(cond);
    }

    case STMT_GOTO: {
        int node = cfg_add_node(builder->cfg, CFG_NODE_STMT, stmt);
        gotos_push(builder, node, stmt->goto_label);
        return no_exit(node);
    }

    case STMT_LABEL: {
        int node = cfg_add_node(builder->cfg, CFG_NODE_STMT, stmt);
        labels_push(builder, stmt->label_name, node);
        return single_exit(node);
    }

    case STMT_DECL:
    case STMT_ASSIGN:
    case STMT_INPUT:
    case STMT_OUTPUT:
    case STMT_APPLY:
        return single_exit(cfg_add_node(builder->cfg, CFG_NODE_STMT, stmt));

    case STMT_BLOCK:
        return build_statements(builder, stmt->block.block_stmts,
                                stmt->block.block_count);
    }

    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
        "unknown statement kind in cfg");
}

/*
  Build a sequence of statements and connect fallthrough edges between them.

  The algorithm keeps the previous statement-list exits open until the next
  statement's first node exists. A final caller decides where the whole list
  falls through.
 */
static BuildResult build_statements(CFGBuilder *builder,
                                    Statement **statements, int count) {
    BuildResult result = {-1, {0}};

    for (int i = 0; i < count; i++) {
        BuildResult current = build_statement(builder, statements[i]);
        if (current.first < 0) {
            intlist_free(&current.exits);
            continue;
        }

        if (result.first < 0) {
            result.first = current.first;
        }

        patch_exits(builder, &result.exits, current.first);
        intlist_free(&result.exits);
        result.exits = current.exits;
    }

    return result;
}

/* Turn all pending gotos into real CFG edges now that labels are known. */
static void resolve_gotos(CFGBuilder *builder) {
    for (int i = 0; i < builder->goto_count; i++) {
        int target = label_lookup(builder, builder->gotos[i].target);
        if (target < 0) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
                "goto target '%s' missing from cfg label map",
                builder->gotos[i].target);
        }

        cfg_add_edge(builder->cfg, builder->gotos[i].node, target);
    }
}

/*
  Public CFG entry point.

  The synthetic entry node makes the start of the program explicit. The
  synthetic exit node gives liveness a single place where all normal paths end.
 */
CFG *cfg_build(ProgramAST *program) {
    if (program == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "null program in cfg_build");
    }

    CFGBuilder builder = {0};
    builder.cfg = cfg_new();
    builder.cfg->entry = cfg_add_node(builder.cfg, CFG_NODE_ENTRY, NULL);
    builder.cfg->exit = cfg_add_node(builder.cfg, CFG_NODE_EXIT, NULL);

    BuildResult body =
        build_statements(&builder, program->statements, program->count);
    if (body.first >= 0) {
        cfg_add_edge(builder.cfg, builder.cfg->entry, body.first);
        patch_exits(&builder, &body.exits, builder.cfg->exit);
    } else {
        cfg_add_edge(builder.cfg, builder.cfg->entry, builder.cfg->exit);
    }
    intlist_free(&body.exits);

    resolve_gotos(&builder);
    free(builder.labels);
    free(builder.gotos);
    return builder.cfg;
}

/* Free the graph storage. AST statements are borrowed and are not freed here. */
void cfg_free(CFG *cfg) {
    if (cfg == NULL) {
        return;
    }

    free(cfg->nodes);
    free(cfg);
}

/* Short stable names for CFG dumps. */
static const char *stmt_kind_name(StatementKind kind) {
    switch (kind) {
    case STMT_DECL:
        return "decl";
    case STMT_ASSIGN:
        return "assign";
    case STMT_IF:
        return "if";
    case STMT_WHILE:
        return "while";
    case STMT_GOTO:
        return "goto";
    case STMT_LABEL:
        return "label";
    case STMT_INPUT:
        return "input";
    case STMT_OUTPUT:
        return "output";
    case STMT_APPLY:
        return "apply";
    case STMT_BLOCK:
        return "block";
    }

    return "stmt";
}

/* Print enough statement detail to debug CFG shape without needing an AST dump. */
static void dump_node_summary(const CFGNode *node, FILE *out) {
    if (node->kind == CFG_NODE_ENTRY) {
        fputs("entry", out);
        return;
    }
    if (node->kind == CFG_NODE_EXIT) {
        fputs("exit", out);
        return;
    }

    Statement *stmt = node->stmt;
    if (stmt == NULL) {
        fputs("stmt <null>", out);
        return;
    }

    switch (stmt->kind) {
    case STMT_DECL:
        fprintf(out, "decl %s", stmt->decl.decl_name);
        break;
    case STMT_ASSIGN:
        fprintf(out, "assign %s", stmt->assign.assign_name);
        break;
    case STMT_IF:
        fputs("if <cond>", out);
        break;
    case STMT_WHILE:
        fputs("while <cond>", out);
        break;
    case STMT_GOTO:
        fprintf(out, "goto %s", stmt->goto_label);
        break;
    case STMT_LABEL:
        fprintf(out, "label %s", stmt->label_name);
        break;
    case STMT_INPUT:
    case STMT_OUTPUT:
    case STMT_APPLY:
    case STMT_BLOCK:
        fputs(stmt_kind_name(stmt->kind), out);
        break;
    }
}

/* Human-readable graph dump used by --dump-cfg and small unit tests. */
void cfg_dump(const CFG *cfg, FILE *out) {
    if (cfg == NULL || out == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "null cfg dump");
    }

    for (int i = 0; i < cfg->count; i++) {
        const CFGNode *node = &cfg->nodes[i];
        fprintf(out, "node %d: ", node->id);
        dump_node_summary(node, out);
        fputs(" ->", out);
        for (int j = 0; j < node->succ_count; j++) {
            fprintf(out, " %d", node->succ[j]);
        }
        fputc('\n', out);
    }
}

/*
  Ensure every int variable can fit in the LiveSet representation before we
  start shifting bits. This includes synthetic temporaries created by the
  desugarer, so the check belongs here rather than in the parser/typechecker.
 */
static void check_liveness_indices(TypeEnv *env) {
    for (int i = 0; i < env->count; i++) {
        if (env->entries[i].type == TYPE_INT &&
            env->entries[i].index >= LIVESET_BITS) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
                "int variable '%s' has index %d outside liveness bitset",
                env->entries[i].name, env->entries[i].index);
        }
    }
}

/*
  Add an int variable to a live set by name.

  Non-int names are ignored because alg variables exist only at compile time and
  never receive cube registers. Missing names are also ignored here; successful
  typechecking is responsible for rejecting undeclared variables earlier.
 */
static void add_int_var(TypeEnv *env, LiveSet *set, const char *name) {
    VarEntry *entry = typeenv_lookup(env, name);
    if (entry == NULL || entry->type != TYPE_INT) {
        return;
    }

    *set |= live_bit(entry->index);
}

/*
  Collect integer variable reads from an expression.

  Alg literals/concats and ord(...) do not create runtime int-register uses
  here. ord(...) should already be folded by desugaring before codegen cares
  about the value.
 */
static void collect_expr_uses(TypeEnv *env, Expr *expr, LiveSet *use) {
    if (expr == NULL) {
        return;
    }

    switch (expr->kind) {
    case EXPR_VAR:
        add_int_var(env, use, expr->var_name);
        break;

    case EXPR_ADD:
    case EXPR_SUB:
    case EXPR_EQ:
    case EXPR_LT:
    case EXPR_GT:
    case EXPR_LEQ:
    case EXPR_GEQ:
    case EXPR_CONCAT:
        collect_expr_uses(env, expr->bin_op.LHS, use);
        collect_expr_uses(env, expr->bin_op.RHS, use);
        break;

    case EXPR_NEG:
    case EXPR_NOT:
        collect_expr_uses(env, expr->unary_op, use);
        break;

    case EXPR_INT_LIT:
    case EXPR_ALG_LIT:
    case EXPR_ORD:
    case EXPR_SOLVED:
        break;
    }
}


/*
  Compute use/def for one CFG node.

  Definitions are only runtime int-register writes. Alg declarations and apply
  statements are compile-time/raw-move concerns, so they do not contribute to
  liveness of allocated int variables.
 */
static void compute_node_use_def(TypeEnv *env, CFGNode *node) {
    node->use = 0;
    node->def = 0;

    if (node->kind != CFG_NODE_STMT || node->stmt == NULL) {
        return;
    }

    Statement *stmt = node->stmt;
    switch (stmt->kind) {
    case STMT_DECL:
        if (stmt->decl.decl_type == TYPE_INT) {
            collect_expr_uses(env, stmt->decl.decl_value, &node->use);
            add_int_var(env, &node->def, stmt->decl.decl_name);
        }
        break;

    case STMT_ASSIGN: {
        VarEntry *entry = typeenv_lookup(env, stmt->assign.assign_name);
        if (entry != NULL && entry->type == TYPE_INT) {
            collect_expr_uses(env, stmt->assign.assign_value, &node->use);
            node->def |= live_bit(entry->index);
        }
        break;
    }

    case STMT_IF:
        collect_expr_uses(env, stmt->if_stmt.if_cond, &node->use);
        break;

    case STMT_WHILE:
        collect_expr_uses(env, stmt->while_stmt.while_cond, &node->use);
        break;

    case STMT_INPUT:
        add_int_var(env, &node->def, R0_SYNTHETIC_VARIABLE);
        break;

    case STMT_OUTPUT:
        if (stmt->output_expr == NULL) {
            add_int_var(env, &node->use, R0_SYNTHETIC_VARIABLE);
        } else {
            collect_expr_uses(env, stmt->output_expr, &node->use);
        }
        break;

    case STMT_GOTO:
    case STMT_LABEL:
    case STMT_APPLY:
    case STMT_BLOCK:
        break;
    }
}

/*
  A user variable live at the synthetic entry usually means "used before any
  definition on some path". The spec asks for a warning rather than a hard
  error, because the typechecker already owns semantic rejection rules.
 */
static void warn_live_at_entry(TypeEnv *env, LiveSet live_in) {
    int r0 = typeenv_index(env, R0_SYNTHETIC_VARIABLE);
    LiveSet user_live = live_in;
    if (r0 >= 0) {
        user_live &= ~live_bit(r0);
    }

    if (user_live == 0) {
        return;
    }

    fputs("[liveness] line 0: variable live at entry:", stderr);
    for (int i = 0; i < env->count && i < LIVESET_BITS; i++) {
        if ((user_live & live_bit(i)) != 0) {
            fprintf(stderr, " %s", env->entries[i].name);
        }
    }
    fputc('\n', stderr);
}

/*
  Run the standard backwards liveness fixpoint:

    live_out[n] = union(live_in[s] for each successor s)
    live_in[n]  = use[n] union (live_out[n] minus def[n])

  Starting from empty sets is safe because the equations only add bits until a
  fixed point is reached. Reverse node order converges quickly on straight-line
  code, while loops simply require more passes.
 */
LivenessResult *liveness_analyze(CFG *cfg, TypeEnv *env) {
    if (cfg == NULL || env == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "null liveness input");
    }

    check_liveness_indices(env);

    for (int i = 0; i < cfg->count; i++) {
        compute_node_use_def(env, &cfg->nodes[i]);
        cfg->nodes[i].live_in = 0;
        cfg->nodes[i].live_out = 0;
    }

    bool changed = true;
    while (changed) {
        changed = false;

        for (int i = cfg->count - 1; i >= 0; i--) {
            CFGNode *node = &cfg->nodes[i];
            LiveSet live_out = 0;

            for (int s = 0; s < node->succ_count; s++) {
                live_out |= cfg->nodes[node->succ[s]].live_in;
            }

            LiveSet live_in = node->use | (live_out & ~node->def);
            if (live_in != node->live_in || live_out != node->live_out) {
                node->live_in = live_in;
                node->live_out = live_out;
                changed = true;
            }
        }
    }

    warn_live_at_entry(env, cfg->nodes[cfg->entry].live_in);

    LivenessResult *result = malloc(sizeof(LivenessResult));
    if (result == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    result->cfg = cfg;
    result->env = env;
    result->node_count = cfg->count;
    result->ig_nodes = malloc(sizeof(IGLivenessNode) * (size_t)cfg->count);
    if (result->ig_nodes == NULL) {
        free(result);
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    for (int i = 0; i < cfg->count; i++) {
        result->ig_nodes[i].def = cfg->nodes[i].def;
        result->ig_nodes[i].live_out = cfg->nodes[i].live_out;
    }

    return result;
}

/* Free only the analysis wrapper. The CFG and TypeEnv are borrowed. */
void liveness_free(LivenessResult *result) {
    if (result == NULL) {
        return;
    }

    free(result->ig_nodes);
    free(result);
}

/* Print a bitset using TypeEnv names so dumps stay readable and deterministic. */
static void dump_liveset(TypeEnv *env, LiveSet set, FILE *out) {
    fputc('{', out);
    bool first = true;

    for (int i = 0; i < env->count && i < LIVESET_BITS; i++) {
        if ((set & live_bit(i)) == 0) {
            continue;
        }

        if (!first) {
            fputc(',', out);
        }
        fputs(env->entries[i].name, out);
        first = false;
    }

    fputc('}', out);
}

/* Human-readable dump for --dump-liveness and regression tests. */
void liveness_dump(const LivenessResult *result, FILE *out) {
    if (result == NULL || result->cfg == NULL || result->env == NULL ||
        out == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "null liveness dump");
    }

    for (int i = 0; i < result->cfg->count; i++) {
        const CFGNode *node = &result->cfg->nodes[i];
        fprintf(out, "node %d: in=", node->id);
        dump_liveset(result->env, node->live_in, out);
        fputs(" out=", out);
        dump_liveset(result->env, node->live_out, out);
        fputs(" use=", out);
        dump_liveset(result->env, node->use, out);
        fputs(" def=", out);
        dump_liveset(result->env, node->def, out);
        fputc('\n', out);
    }
}
