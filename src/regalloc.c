#include "../include/regalloc.h"
#include "../include/alg.h"
#include "../include/ccs.h"
#include "../include/interference.h"
#include "../include/piece.h"
#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* IDA* is only a fallback when no Kociemba CCS template fits. Its cost grows
 * ~13^depth, so this stays modest: deep enough to beat the original depth 6,
 * shallow enough that an infeasible request fails in seconds rather than
 * minutes. The order-aware leaf filter (see idas_dfs) is what actually lets a
 * shorter ceiling still find useful high-order registers. */
#ifndef REGALLOC_IDA_MAX_DEPTH
#define REGALLOC_IDA_MAX_DEPTH 7
#endif

#ifndef REGALLOC_CACHE_PATH
#define REGALLOC_CACHE_PATH "regalloc-registers.bin"
#endif

#define MOVE_COUNT 18

/* --- IDA* constants ---
 * All 18 moves in fixed canonical order: UDLRFB, q=1,2,3.
 */

static const Move ALL_MOVES[MOVE_COUNT] = {
    {FACE_U, 1}, {FACE_U, 2}, {FACE_U, 3},
    {FACE_D, 1}, {FACE_D, 2}, {FACE_D, 3},
    {FACE_L, 1}, {FACE_L, 2}, {FACE_L, 3},
    {FACE_R, 1}, {FACE_R, 2}, {FACE_R, 3},
    {FACE_F, 1}, {FACE_F, 2}, {FACE_F, 3},
    {FACE_B, 1}, {FACE_B, 2}, {FACE_B, 3},
};

static const PieceLabel CORNER_PIECES[] = {
    PC_UFL, PC_UFR, PC_UBL, PC_UBR, PC_DFL, PC_DFR, PC_DBL, PC_DBR,
};

static const PieceLabel EDGE_PIECES[] = {
    PC_UF, PC_UB, PC_UL, PC_UR, PC_DF, PC_DB, PC_DL, PC_DR,
    PC_FL, PC_FR, PC_BL, PC_BR,
};

#define REG_CANDIDATE_MAX_CYCLES 4

typedef struct {
    uint8_t orbit;           /* 0 = corners, 1 = edges */
    uint8_t length;
    int8_t  net_orientation;
} RegCycleCandidate;

typedef struct {
    int order;
    int num_cycles;
    RegCycleCandidate cycles[REG_CANDIDATE_MAX_CYCLES];
} RegCandidate;

static const RegCandidate REG_CANDIDATES[] = {
    { 2, 2, {{1,1,1}, {1,1,1}} },
    { 2, 2, {{1,2,0}, {0,2,0}} },
    { 3, 1, {{1,3,0}} },
    { 3, 1, {{0,3,0}} },
    { 4, 2, {{1,2,1}, {1,2,1}} },
    { 4, 2, {{1,4,0}, {0,2,0}} },
    { 4, 2, {{0,4,0}, {1,2,0}} },
    { 5, 1, {{1,5,0}} },
    { 6, 3, {{1,3,0}, {1,1,1}, {1,1,1}} },
    { 6, 3, {{1,3,0}, {1,2,0}, {0,2,0}} },
    { 7, 1, {{1,7,0}} },
    { 8, 2, {{1,4,1}, {1,2,1}} },
    { 8, 2, {{1,8,0}, {0,2,0}} },
    { 9, 1, {{1,9,0}} },
    { 9, 2, {{0,3,1}, {0,1,2}} },
    {10, 3, {{1,5,0}, {1,1,1}, {1,1,1}} },
    {10, 3, {{1,5,0}, {1,2,0}, {0,2,0}} },
    {11, 1, {{1,11,0}} },
    {12, 3, {{1,3,0}, {1,2,1}, {1,2,1}} },
    {12, 3, {{1,4,0}, {1,3,0}, {0,2,0}} },
    {14, 3, {{1,7,0}, {1,1,1}, {1,1,1}} },
    {15, 2, {{1,5,0}, {1,3,0}} },
    {16, 2, {{1,8,1}, {1,2,1}} },
    {18, 3, {{1,9,0}, {1,1,1}, {1,1,1}} },
    {20, 3, {{1,5,0}, {1,2,1}, {1,2,1}} },
    {21, 2, {{1,7,0}, {1,3,0}} },
    {24, 3, {{1,3,0}, {1,4,1}, {1,2,1}} },
    {28, 3, {{1,7,0}, {1,2,1}, {1,2,1}} },
    {30, 4, {{1,5,0}, {1,3,0}, {1,1,1}, {1,1,1}} },
    {35, 2, {{1,7,0}, {1,5,0}} },
    {42, 4, {{1,7,0}, {1,3,0}, {1,1,1}, {1,1,1}} },
    {60, 4, {{1,5,0}, {1,3,0}, {1,2,1}, {1,2,1}} },
};

typedef struct {
    int      order;
    CycleSet cycles;
    char    *algorithm;
} RegCacheEntry;

static RegCacheEntry *g_regcache = NULL;
static int g_regcache_count = 0;
static int g_regcache_cap = 0;
static bool g_regcache_loaded = false;

#define REGCACHE_MAGIC   0x52474343u /* "RGCC", little-endian on disk */
#define REGCACHE_VERSION 1u
#define REGCACHE_MAX_ALG_LEN 4096u

static void regcache_push_owned(int order, CycleSet cycles, char *algorithm) {
    if (g_regcache_count == g_regcache_cap) {
        int new_cap = next_cap(g_regcache_cap);
        RegCacheEntry *new_entries = realloc(
            g_regcache, (size_t)new_cap * sizeof *new_entries);
        if (!new_entries)
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "OOM: regcache");
        g_regcache = new_entries;
        g_regcache_cap = new_cap;
    }

    g_regcache[g_regcache_count++] = (RegCacheEntry){
        .order = order,
        .cycles = cycles,
        .algorithm = algorithm,
    };
}

static bool regcache_read_u32(FILE *fp, uint32_t *out) {
    return fread(out, sizeof *out, 1, fp) == 1;
}

static void regcache_load(void) {
    if (g_regcache_loaded) return;
    g_regcache_loaded = true;

    FILE *fp = fopen(REGALLOC_CACHE_PATH, "rb");
    if (!fp) return;

    uint32_t magic = 0, version = 0, count = 0;
    if (!regcache_read_u32(fp, &magic) ||
        !regcache_read_u32(fp, &version) ||
        !regcache_read_u32(fp, &count) ||
        magic != REGCACHE_MAGIC ||
        version != REGCACHE_VERSION) {
        fclose(fp);
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t order = 0, cycles = 0, alg_len = 0;
        if (!regcache_read_u32(fp, &order) ||
            !regcache_read_u32(fp, &cycles) ||
            !regcache_read_u32(fp, &alg_len) ||
            alg_len == 0 ||
            alg_len > REGCACHE_MAX_ALG_LEN) {
            break;
        }

        char *algorithm = malloc((size_t)alg_len + 1);
        if (!algorithm) die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "OOM: regcache_load");
        if (fread(algorithm, 1, alg_len, fp) != alg_len) {
            free(algorithm);
            break;
        }
        algorithm[alg_len] = '\0';
        regcache_push_owned((int)order, (CycleSet)cycles, algorithm);
    }

    fclose(fp);
}

static void regcache_save(void) {
    FILE *fp = fopen(REGALLOC_CACHE_PATH, "wb");
    if (!fp) return;

    uint32_t magic = REGCACHE_MAGIC;
    uint32_t version = REGCACHE_VERSION;
    uint32_t count = (uint32_t)g_regcache_count;
    if (fwrite(&magic, sizeof magic, 1, fp) != 1 ||
        fwrite(&version, sizeof version, 1, fp) != 1 ||
        fwrite(&count, sizeof count, 1, fp) != 1) {
        fclose(fp);
        return;
    }

    for (int i = 0; i < g_regcache_count; i++) {
        uint32_t order = (uint32_t)g_regcache[i].order;
        uint32_t cycles = (uint32_t)g_regcache[i].cycles;
        size_t len = strlen(g_regcache[i].algorithm);
        if (len == 0 || len > REGCACHE_MAX_ALG_LEN) continue;
        uint32_t alg_len = (uint32_t)len;
        if (fwrite(&order, sizeof order, 1, fp) != 1 ||
            fwrite(&cycles, sizeof cycles, 1, fp) != 1 ||
            fwrite(&alg_len, sizeof alg_len, 1, fp) != 1 ||
            fwrite(g_regcache[i].algorithm, 1, len, fp) != len) {
            break;
        }
    }

    fclose(fp);
}

static bool regcache_lookup(int order, CycleSet cycles, CycleSet forbidden,
                            Alg *out) {
    regcache_load();

    for (int i = 0; i < g_regcache_count; i++) {
        if (g_regcache[i].order != order || g_regcache[i].cycles != cycles)
            continue;

        Alg alg = {0};
        if (!alg_parse(g_regcache[i].algorithm, &alg)) continue;
        if (compute_order(&alg) == order &&
            cycleset_from_alg(&alg) == cycles &&
            cycleset_disjoint(cycles, forbidden)) {
            *out = alg;
            return true;
        }
        alg_free(&alg);
    }

    return false;
}

static void regcache_store(int order, CycleSet cycles, const Alg *alg) {
    regcache_load();

    for (int i = 0; i < g_regcache_count; i++)
        if (g_regcache[i].order == order && g_regcache[i].cycles == cycles)
            return;

    char *algorithm = alg_to_string(alg);
    if (!algorithm) return;
    regcache_push_owned(order, cycles, algorithm);
    regcache_save();
}

/* --- Register table --- */

/* Append an entry to the register table, doubling capacity as needed. */
static void regtable_push(RegTable *t, const RegEntry entry) {
    if (t->count == t->cap) {
        const int newcap = next_cap(t->cap);
        t->regs = realloc(t->regs, (size_t)newcap * sizeof(RegEntry));
        if (!t->regs) die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "OOM: regtable_push");
        t->cap = newcap;
    }
    t->regs[t->count++] = entry;
}

static int find_and_add_impl(RegTable *table, const CycleSet forbidden,
                             const int required_order, bool prefer_corners);

/* Initialise the table and pre-populate R0 plus reserved temp R1.
 *
 * temp_required_order is the largest register order any int variable in the
 * program demands. The scratch register R1 backs every variable add/sub, and
 * codegen requires temp order >= source order, so the temp must be at least as
 * large as the widest variable or those operations are rejected. Passing 0
 * keeps the historical order-3 scratch register. */
void regalloc_init(RegTable *table, int temp_required_order) {
    *table = (RegTable){0};

    Alg r0 = {0};
    if (!alg_parse(R0_ALGORITHM, &r0))
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
            "regalloc_init: could not parse R0 algorithm \"%s\"", R0_ALGORITHM);

    const CycleSet cs = cycleset_from_alg(&r0);
    const int order = compute_order(&r0);
    char *alg_str = alg_to_string(&r0);
    alg_free(&r0);

    regtable_push(table, (RegEntry){
        .algorithm = alg_str,
        .order     = order,
        .cycles    = cs,
        .index     = 0,
    });
    table->r0_reserved = true;

    /* R1 is reserved for codegen's destructive copy/compare helpers. It must be
     * wide enough to hold any variable that takes part in an add/sub, and we bias
     * it toward a dense corner register so the 12 edges stay free for ordinary
     * variables (see candidate_is_corner_only). */
    find_and_add_impl(table, table->regs[0].cycles, temp_required_order, true);
}

/* Free all owned algorithm strings and the entry array. */
void regalloc_free(RegTable *table) {
    for (int i = 0; i < table->count; i++) free(table->regs[i].algorithm);
    free(table->regs);
    *table = (RegTable){0};
}

/* --- IDA* search ---
 * Pruning rules (cuts branching factor from 18 to ~13):
 *   1. Never repeat the same face.
 *   2. Opposite-face canonical order: U<D, L<R, F<B (face^1 gives the
 *      opposite; skip if opposite and prev > cur, e.g. no U after D).
 *
 * Leaf check uses the running CubeState directly rather than re-applying
 * the path: cycle set is non-empty iff the sequence is non-identity
 * (equivalent to ord > 1), so compute_order is not needed at leaves.
 */

/* Derive a CycleSet from a live cube state: any displaced facelet marks its piece. */
static CycleSet cycleset_from_state(const CubeState *s) {
    CycleSet cs = CYCLESET_EMPTY;
    for (int i = 0; i < FACELET_COUNT; i++)
        if (s->state[i] != (uint8_t)i)
            cs |= (1u << (unsigned)facelet_to_piece[i]);
    return cs;
}

/* True if this move should be skipped: same face repeated, or opposite-face pair out of canonical order. */
static bool should_prune(const int prev_face, const int cur_face) {
    if (prev_face < 0) return false;
    if (cur_face == prev_face) return true;
    if ((cur_face ^ 1) == prev_face && prev_face > cur_face) return true;
    return false;
}

/* Write the first `depth` moves of path into out (growing as needed). */
static void idas_emit_path(const Move *path, const int depth, Alg *out) {
    out->len = 0;
    for (int i = 0; i < depth; i++) {
        if (out->len == out->cap) {
            const int nc = next_cap(out->cap);
            out->m = realloc(out->m, (size_t)nc * sizeof(Move));
            if (!out->m) die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "OOM: idas_dfs");
            out->cap = nc;
        }
        out->m[out->len++] = path[i];
    }
}

/* DFS leg of IDA*: extend the path one move at a time; write the found sequence into out on success.
 *
 * A leaf is accepted only when its cycle set is non-empty, disjoint from
 * forbidden, and — when required_order > 0 — when the realised algorithm's order
 * is at least required_order. The order filter is what lets the iterative
 * deepening keep searching deeper for a high-order register instead of latching
 * onto the first (shortest, lowest-order) disjoint sequence it stumbles on. */
static bool idas_dfs(const CubeState cur, Move *path, const int depth, const int limit,
                     const int prev_face, const CycleSet forbidden,
                     const int required_order, Alg *out) {
    if (depth == limit) {
        const CycleSet cs = cycleset_from_state(&cur);
        if (cs == CYCLESET_EMPTY) return false;
        if (!cycleset_disjoint(cs, forbidden)) return false;
        idas_emit_path(path, depth, out);
        if (required_order > 0 && compute_order(out) < required_order) {
            out->len = 0;
            return false;
        }
        return true;
    }

    for (int mi = 0; mi < MOVE_COUNT; mi++) {
        const int face = (int)ALL_MOVES[mi].face;
        if (should_prune(prev_face, face)) continue;
        path[depth] = ALL_MOVES[mi];
        CubeState next = cur;
        cube_apply_move(&next, (Face)face, (int)ALL_MOVES[mi].q);
        if (idas_dfs(next, path, depth + 1, limit, face, forbidden, required_order, out))
            return true;
    }
    return false;
}

/* Iterative-deepening wrapper: fallback DFS after CCS candidates are exhausted.
 * required_order == 0 means "any disjoint algorithm"; a positive value keeps
 * deepening until a disjoint algorithm of at least that order is found. */
static void idas_find(const CycleSet forbidden, const int required_order, Alg *out) {
    CubeState id;
    cube_identity(&id);
    Move path[REGALLOC_IDA_MAX_DEPTH];

    for (int limit = 1; limit <= REGALLOC_IDA_MAX_DEPTH; limit++)
        if (idas_dfs(id, path, 0, limit, -1, forbidden, required_order, out)) return;

    die(EXIT_CODE_REGALLOC, STAGE_REGALLOC, -1,
        "register pressure too high: no disjoint cube algorithm of order >= %d found "
        "via CCS or IDA search depth %d",
        required_order, REGALLOC_IDA_MAX_DEPTH);
}

static bool piece_allowed(PieceLabel p, CycleSet forbidden) {
    return (forbidden & (CycleSet)(1u << (unsigned)p)) == 0;
}

static int regalloc_gcd(int a, int b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static int candidate_cycle_order(const RegCycleCandidate *cy) {
    int period = (cy->orbit == 0) ? 3 : 2;
    int g = regalloc_gcd((int)cy->net_orientation, period);
    if (g == 0) g = period;
    return cy->length * (period / g);
}

static int collect_allowed_pieces(const PieceLabel src[], int count,
                                  CycleSet forbidden, PieceLabel dst[]) {
    int n = 0;
    for (int i = 0; i < count; i++)
        if (piece_allowed(src[i], forbidden)) dst[n++] = src[i];
    return n;
}

static bool build_candidate_register(const RegCandidate *candidate,
                                     CycleSet forbidden, CCSRegister *reg) {
    PieceLabel corners[sizeof CORNER_PIECES / sizeof CORNER_PIECES[0]];
    PieceLabel edges[sizeof EDGE_PIECES / sizeof EDGE_PIECES[0]];
    int corner_count = collect_allowed_pieces(
        CORNER_PIECES, (int)(sizeof CORNER_PIECES / sizeof CORNER_PIECES[0]),
        forbidden, corners);
    int edge_count = collect_allowed_pieces(
        EDGE_PIECES, (int)(sizeof EDGE_PIECES / sizeof EDGE_PIECES[0]),
        forbidden, edges);
    int corner_pos = 0;
    int edge_pos = 0;

    *reg = (CCSRegister){0};
    reg->abstract.order = candidate->order;
    reg->abstract.num_cycles = candidate->num_cycles;

    for (int i = 0; i < candidate->num_cycles; i++) {
        const RegCycleCandidate *src = &candidate->cycles[i];
        const int f = (src->orbit == 0) ? 3 : 2;
        PieceLabel *pool = (src->orbit == 0) ? corners : edges;
        int *pos = (src->orbit == 0) ? &corner_pos : &edge_pos;
        int count = (src->orbit == 0) ? corner_count : edge_count;

        if (*pos + src->length > count) return false;

        reg->abstract.cycles[i] = (CCFCycle){
            .orbit = src->orbit,
            .length = src->length,
            .net_orientation = src->net_orientation,
            .order = candidate_cycle_order(src),
        };
        reg->cycles[i].abstract = reg->abstract.cycles[i];

        for (int j = 0; j < src->length; j++) {
            reg->cycles[i].pieces[j] = pool[(*pos)++];
            reg->cycles[i].ori_deltas[j] = 0;
        }
        if (src->net_orientation != 0) {
            reg->cycles[i].ori_deltas[0] =
                (int8_t)(((src->net_orientation % f) + f) % f);
        }
    }

    return true;
}

static CycleSet register_candidate_cycleset(const CCSRegister *reg) {
    CycleSet cs = CYCLESET_EMPTY;
    for (int c = 0; c < reg->abstract.num_cycles; c++) {
        const CCSCycle *cy = &reg->cycles[c];
        for (int i = 0; i < cy->abstract.length; i++)
            cs |= (CycleSet)(1u << (unsigned)cy->pieces[i]);
    }
    return cs;
}

static bool try_ccs_candidate(const RegCandidate *candidate,
                              CycleSet forbidden, Alg *out) {
    CCSRegister reg = {0};
    if (!build_candidate_register(candidate, forbidden, &reg)) return false;

    CycleSet want = register_candidate_cycleset(&reg);
    if (regcache_lookup(candidate->order, want, forbidden, out)) return true;

    Alg alg = {0};
    if (!ccs_find_alg(&reg, &alg)) return false;

    CycleSet cs = cycleset_from_alg(&alg);
    int order = compute_order(&alg);
    if (order == candidate->order && cs == want && cycleset_disjoint(cs, forbidden)) {
        regcache_store(order, cs, &alg);
        *out = alg;
        return true;
    }

    alg_free(&alg);
    return false;
}

/* True when every cycle of the candidate lives in the corner orbit. Corner
 * registers are "dense": three orientations per piece means a 3- or 4-cycle of
 * corners reaches order 9 or 12 on just 3-4 pieces, where the same order on
 * edges would need a long cycle. Reserving such a register for the scratch slot
 * leaves the roomy 12-edge space free for ordinary variables. */
static bool candidate_is_corner_only(const RegCandidate *c) {
    for (int i = 0; i < c->num_cycles; i++)
        if (c->cycles[i].orbit != 0) return false;
    return true;
}

/* Prefer compact Kociemba-backed cycle templates before the old bounded blind
 * search. For constrained variables, try the requested order and then larger
 * known templates incrementally. When prefer_corners is set, corner-only
 * templates are tried first so the chosen register is dense (see
 * candidate_is_corner_only). */
static bool ccs_find_small_register(CycleSet forbidden, int required_order,
                                    bool prefer_corners, Alg *out) {
    int n = (int)(sizeof REG_CANDIDATES / sizeof REG_CANDIDATES[0]);

    if (required_order == 0 && !prefer_corners) {
        for (int i = 0; i < n; i++) {
            if (REG_CANDIDATES[i].order != 3) continue;
            if (try_ccs_candidate(&REG_CANDIDATES[i], forbidden, out))
                return true;
        }
    }

    if (prefer_corners) {
        for (int i = 0; i < n; i++) {
            if (REG_CANDIDATES[i].order < required_order) continue;
            if (!candidate_is_corner_only(&REG_CANDIDATES[i])) continue;
            if (try_ccs_candidate(&REG_CANDIDATES[i], forbidden, out))
                return true;
        }
    }

    for (int i = 0; i < n; i++) {
        if (REG_CANDIDATES[i].order < required_order) continue;
        if (try_ccs_candidate(&REG_CANDIDATES[i], forbidden, out))
            return true;
    }

    return false;
}

/* Shared implementation: find a disjoint algorithm, append it, return its index.
 * prefer_corners biases the search toward dense corner registers, used for the
 * reserved scratch register so variables keep the edge space. */
static int find_and_add_impl(RegTable *table, const CycleSet forbidden,
                             const int required_order, bool prefer_corners) {
    Alg alg = {0};
    if (!ccs_find_small_register(forbidden, required_order, prefer_corners, &alg)) {
        idas_find(forbidden, required_order, &alg);
    }

    const int order   = compute_order(&alg);
    if (required_order > 0 && order < required_order) {
        alg_free(&alg);
        die(EXIT_CODE_REGALLOC, STAGE_REGALLOC, -1,
            "no disjoint algorithm with order >= %d found "
            "(CCS tried known templates up to order 60; "
            "IDA fallback depth %d)", required_order, REGALLOC_IDA_MAX_DEPTH);
    }

    const int new_idx = table->count;
    char *alg_str     = alg_to_string(&alg);
    const CycleSet cs = cycleset_from_alg(&alg);
    alg_free(&alg);

    regtable_push(table, (RegEntry){
        .algorithm = alg_str,
        .order     = order,
        .cycles    = cs,
        .index     = new_idx,
    });
    return new_idx;
}

/* Find a new algorithm disjoint from forbidden, append it to the table, and
 * return its index. Compact CCS/Kociemba templates are preferred; bounded IDA*
 * remains as a fallback, mainly for non-3 order requests. */
int regalloc_find_and_add(RegTable *table, const CycleSet forbidden,
                          const int required_order) {
    return find_and_add_impl(table, forbidden, required_order, false);
}

/* --- Graph colouring --- */

/* Sort IGNode pointers: highest degree first, alphabetical on ties. */
static int cmp_node_degree_desc(const void *ap, const void *bp) {
    const IGNode *a = *(const IGNode *const *)ap;
    const IGNode *b = *(const IGNode *const *)bp;
    const int da = __builtin_popcountll((unsigned long long)a->neighbours);
    const int db = __builtin_popcountll((unsigned long long)b->neighbours);
    if (da != db) return db - da;
    return strcmp(a->name, b->name);
}

/* Build forbidden CycleSet: reserved registers plus every coloured neighbour's cycles. */
static CycleSet build_forbidden(const RegTable *table, const IGNode *node,
                                const int *coloring) {
    CycleSet forbidden = table->r0_reserved ? table->regs[0].cycles : CYCLESET_EMPTY;
    if (table->count > 1) {
        forbidden |= table->regs[1].cycles;
    }
    for (uint64_t bits = (uint64_t)node->neighbours; bits; bits &= bits - 1) {
        const int j = __builtin_ctzll(bits);
        if (coloring[j] >= 0)
            forbidden |= table->regs[coloring[j]].cycles;
    }
    return forbidden;
}

/* First usable register whose cycles/order fit, or -1. R1 is always temp-only. */
static int find_existing_reg(const RegTable *table, const CycleSet forbidden,
                             const int required_order) {
    for (int ri = 0; ri < table->count; ri++) {
        if (ri == 1) continue;
        if (ri == 0 && table->r0_reserved) continue;
        if (required_order > 0 && table->regs[ri].order < required_order)
            continue;
        if (cycleset_disjoint(table->regs[ri].cycles, forbidden))
            return ri;
    }
    return -1;
}

/* Verify: every assigned register is disjoint from reserved registers and from every
 * interfering neighbour. die(EXIT_CODE_INTERNAL) on violation. */
static void check_invariants(const RegTable *table, const InterferenceGraph *ig,
                              const int *coloring) {
    for (int i = 0; i < ig->count; i++) {
        const int a_idx = ig->nodes[i].var_index;
        const int ra    = coloring[a_idx];
        if (ra < 0)
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
                "regalloc invariant: \"%s\" was not assigned a register",
                ig->nodes[i].name);
        if (table->r0_reserved &&
            !cycleset_disjoint(table->regs[ra].cycles, table->regs[0].cycles))
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
                "regalloc invariant: \"%s\" assigned R%d overlaps R0",
                ig->nodes[i].name, ra);
        if (ra == 1 || !cycleset_disjoint(table->regs[ra].cycles, table->regs[1].cycles))
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
                "regalloc invariant: \"%s\" assigned R%d overlaps reserved temp R1",
                ig->nodes[i].name, ra);
        for (uint64_t bits = (uint64_t)ig->nodes[i].neighbours; bits; bits &= bits - 1) {
            const int b_idx = __builtin_ctzll(bits);
            const int rb    = coloring[b_idx];
            if (rb < 0) continue;
            if (!cycleset_disjoint(table->regs[ra].cycles, table->regs[rb].cycles))
                die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
                    "regalloc invariant: interfering variables share overlapping registers R%d and R%d", ra, rb);
        }
    }
}

/* Colour every IG node with a disjoint register; return colouring[var_index] = RegTable index. */
int *regalloc_run(RegTable *table, const InterferenceGraph *ig) {
    const int env_count = ig->env->count;

    int *coloring = malloc((size_t)env_count * sizeof(int));
    if (!coloring) die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "OOM: regalloc_run");
    for (int i = 0; i < env_count; i++) coloring[i] = -1;
    coloring[0] = 0; /* R0 pre-assigned */

    /* Sort nodes by degree desc, alphabetical tiebreak. */
    IGNode **sorted = malloc((size_t)ig->count * sizeof(IGNode *));
    if (!sorted) die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "OOM: regalloc_run sort");
    for (int i = 0; i < ig->count; i++) sorted[i] = &ig->nodes[i];
    qsort(sorted, (size_t)ig->count, sizeof(IGNode *), cmp_node_degree_desc);

    for (int i = 0; i < ig->count; i++) {
        const IGNode *node       = sorted[i];
        const int required       = node->required_order;
        const CycleSet forbidden = build_forbidden(table, node, coloring);
        int ri = find_existing_reg(table, forbidden, required);
        if (ri < 0) ri = regalloc_find_and_add(table, forbidden, required);
        coloring[node->var_index] = ri;
    }

    free(sorted);
    check_invariants(table, ig, coloring);
    return coloring;
}

/* --- Dump --- */

/* Sort IGNode pointers alphabetically by name. */
static int cmp_node_name_asc(const void *ap, const void *bp) {
    const IGNode *a = *(const IGNode *const *)ap;
    const IGNode *b = *(const IGNode *const *)bp;
    return strcmp(a->name, b->name);
}

/* Print a CycleSet as a brace-enclosed comma-separated piece list, e.g. {UF,UFR,UR}. */
static void cycleset_dump(const CycleSet cs, FILE *out) {
    fputc('{', out);
    bool first = true;
    for (int p = 0; p < PC_COUNT; p++) {
        if (cs & (1u << (unsigned)p)) {
            if (!first) fputc(',', out);
            fputs(piece_to_string(p), out);
            first = false;
        }
    }
    fputc('}', out);
}

/* Print one "var -> Ri  alg=... K=n C={...}" line per variable, sorted by name. */
void regalloc_dump(const RegTable *table, const InterferenceGraph *ig,
                   const int *coloring, FILE *out) {
    if (table->r0_reserved) {
        fprintf(out, "%s -> R0  alg=\"%s\" K=%d C=",
                R0_SYNTHETIC_VARIABLE,
                table->regs[0].algorithm,
                table->regs[0].order);
        cycleset_dump(table->regs[0].cycles, out);
        fputc('\n', out);
    }

    IGNode **sorted = malloc((size_t)ig->count * sizeof(IGNode *));
    if (!sorted) die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "OOM: regalloc_dump");
    for (int i = 0; i < ig->count; i++) sorted[i] = &ig->nodes[i];
    qsort(sorted, (size_t)ig->count, sizeof(IGNode *), cmp_node_name_asc);

    for (int i = 0; i < ig->count; i++) {
        const int ri = coloring[sorted[i]->var_index];
        fprintf(out, "%s -> R%d  alg=\"%s\" K=%d C=",
                sorted[i]->name, ri,
                table->regs[ri].algorithm,
                table->regs[ri].order);
        cycleset_dump(table->regs[ri].cycles, out);
        fputc('\n', out);
    }

    free(sorted);
}
