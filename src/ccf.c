#include "../include/ccf.h"
#include "../include/cube.h"  /* MAX_3X3_ORDER */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

/*
 * 3x3 cube piece counts and orientation periods.
 */
#define CORNER_COUNT  8
#define EDGE_COUNT   12
#define CORNER_PERIOD 3   /* orientation period for corners */
#define EDGE_PERIOD   2   /* orientation period for edges   */
#define MAX_REG      10

/* --------------------------------------------------------------------------
 * Step 2: composite order enumeration
 * -------------------------------------------------------------------------- */

#define MAX_DISTINCT_PRIMES 5

typedef struct {
    const CCFPrimePower *powers;
    int                  power_count;
    CCFOrderSpec        *out;
    int                  cap;
    int                  count;
    CCFPrimePower        cur_factors[CCF_MAX_PRIME_FACTORS];
    int                  cur_num_factors;
    int                  primes[MAX_DISTINCT_PRIMES];
    int                  num_primes;
} OrderSpecCtx;

static void order_spec_rec(OrderSpecCtx *ctx, int prime_idx,
                            int order, int min_pieces) {
    if (prime_idx == ctx->num_primes) {
        if (order > 1 && ctx->count < ctx->cap) {
            CCFOrderSpec *s  = &ctx->out[ctx->count++];
            s->order         = order;
            s->num_factors   = ctx->cur_num_factors;
            for (int i = 0; i < ctx->cur_num_factors; i++)
                s->factors[i] = ctx->cur_factors[i];
            s->min_corners = 0;
            s->min_edges   = 0;
            for (int i = 0; i < ctx->cur_num_factors; i++) {
                s->min_corners += ctx->cur_factors[i].min_pieces;
                if (!ctx->cur_factors[i].fits_corners)
                    s->min_edges += ctx->cur_factors[i].min_pieces;
            }
        }
        return;
    }

    int p = ctx->primes[prime_idx];

    /* skip this prime (p^0 = 1, contributes nothing) */
    order_spec_rec(ctx, prime_idx + 1, order, min_pieces);

    /* try each available power of p */
    for (int i = 0; i < ctx->power_count; i++) {
        const CCFPrimePower *pp = &ctx->powers[i];
        if (pp->p != p) continue;
        int new_min = min_pieces + pp->min_pieces;
        if (new_min > CCF_PIECE_COUNT) continue;
        ctx->cur_factors[ctx->cur_num_factors++] = *pp;
        order_spec_rec(ctx, prime_idx + 1, order * pp->value, new_min);
        ctx->cur_num_factors--;
    }
}

int ccf_order_specs(const CCFPrimePower *powers, int power_count,
                    CCFOrderSpec *out, int cap) {
    OrderSpecCtx ctx = {0};
    ctx.powers      = powers;
    ctx.power_count = power_count;
    ctx.out         = out;
    ctx.cap         = cap;

    for (int i = 0; i < power_count; i++) {
        int p = powers[i].p;
        if (p < 2) continue;
        int seen = 0;
        for (int j = 0; j < ctx.num_primes; j++)
            if (ctx.primes[j] == p) { seen = 1; break; }
        if (!seen)
            ctx.primes[ctx.num_primes++] = p;
    }

    order_spec_rec(&ctx, 0, 1, 0);
    return ctx.count;
}

/* Append a copy of *arch to result, growing the buffer if needed.
 * Returns a pointer to the stored copy, or NULL on allocation failure. */
static CCFArchitecture *result_append(CCFResult *result,
                                      const CCFArchitecture *arch) {
    if (result->count == result->cap) {
        int new_cap = result->cap ? result->cap * 2 : 8;
        CCFArchitecture *buf = realloc(result->archs,
                                       new_cap * sizeof *buf);
        if (!buf) return NULL;
        result->archs = buf;
        result->cap   = new_cap;
    }
    result->archs[result->count] = *arch;
    return &result->archs[result->count++];
}

/* --------------------------------------------------------------------------
 * Step 3: architecture search
 * -------------------------------------------------------------------------- */

static int cmp_spec_desc(const void *a, const void *b) {
    return ((const CCFOrderSpec *)b)->order - ((const CCFOrderSpec *)a)->order;
}

typedef struct {
    const CCFOrderSpec  *specs;
    int                  spec_count;
    CCFResult           *result;
    const CCFOrderSpec  *cur[CCF_MAX_REGISTERS];
    int                  cur_count;
    int                  cur_min_edges;
    int                  cur_min_total; /* sum of min_pieces across all registers */
} ArchCtx;


/* Returns pieces needed to place pp in orbit (0=corners, 1=edges),
 * 0 for a pure-orientation placement, -1 if it cannot fit. */
static int pp_cost(const CCFPrimePower *pp, int orbit) {
    int size   = (orbit == 0) ? CCF_CORNER_COUNT  : CCF_EDGE_COUNT;
    int period = (orbit == 0) ? CCF_CORNER_PERIOD : CCF_EDGE_PERIOD;
    int pm = 1;
    for (int m = 0; m <= pp->k; m++, pm *= pp->p) {
        if (pm > size) break;
        if (period % (pp->value / pm) == 0)
            return (pm == 1) ? 0 : pm;
    }
    return -1;
}

#define MAX_TOTAL_FACTORS (CCF_MAX_REGISTERS * CCF_MAX_PRIME_FACTORS)

typedef struct {
    CCFPrimePower pp;
    int           reg_idx; /* which register this factor belongs to */
    int           orbit;   /* 0=corners, 1=edges — set during recursion */
    int           cost;    /* pieces consumed in that orbit */
} PlacedFactor;

typedef struct {
    ArchCtx      *arch;
    PlacedFactor  factors[MAX_TOTAL_FACTORS];
    int           num_factors;
} PlaceCtx;

static void place_rec(PlaceCtx *ctx, int fi,
                      int used_corners, int used_edges) {
    if (fi == ctx->num_factors) {
        int free_corners = CCF_CORNER_COUNT - used_corners;
        int free_edges   = CCF_EDGE_COUNT   - used_edges;

        /* orientation check: if an orbit has no free pieces, cycle net
         * orientations in that orbit must already sum to 0 mod period */
        int corner_ori = 0, edge_ori = 0;
        for (int i = 0; i < ctx->num_factors; i++) {
            const PlacedFactor *f = &ctx->factors[i];
            int length = (f->cost == 0) ? 1 : f->cost;
            int period = (f->orbit == 0) ? CCF_CORNER_PERIOD : CCF_EDGE_PERIOD;
            int net_ori = ((length * period) / f->pp.value == period) ? 0 : 1;
            if (f->orbit == 0) corner_ori += net_ori;
            else               edge_ori   += net_ori;
        }
        if (free_corners == 0 && corner_ori % CCF_CORNER_PERIOD != 0) return;
        if (free_edges   == 0 && edge_ori   % CCF_EDGE_PERIOD   != 0) return;

        /* parity check: if there are no free pieces available for a
         * compensating 2-swap, the combined cycle parity must be even */
        int parity = 0;
        for (int i = 0; i < ctx->num_factors; i++)
            parity ^= (ctx->factors[i].cost == 0)
                      ? 0 : (ctx->factors[i].cost - 1) & 1;
        if (parity && free_corners < 2 && free_edges < 2) return;

        /* build and record the architecture */
        CCFArchitecture arch = {0};
        arch.num_registers = ctx->arch->cur_count;
        arch.free_corners  = free_corners;
        arch.free_edges    = free_edges;
        for (int r = 0; r < ctx->arch->cur_count; r++) {
            CCFRegister *reg = &arch.registers[r];
            reg->order = ctx->arch->cur[r]->order;
            for (int i = 0; i < ctx->num_factors; i++) {
                const PlacedFactor *f = &ctx->factors[i];
                if (f->reg_idx != r) continue;
                CCFCycle *cy   = &reg->cycles[reg->num_cycles++];
                cy->orbit      = (uint8_t)f->orbit;
                cy->length     = (uint8_t)((f->cost == 0) ? 1 : f->cost);
                cy->order      = f->pp.value;
                int period     = (f->orbit == 0) ? CCF_CORNER_PERIOD
                                                 : CCF_EDGE_PERIOD;
                cy->net_orientation = (int8_t)(
                    (cy->length * period / cy->order == period) ? 0 : 1);
            }
        }
        result_append(ctx->arch->result, &arch);
        return;
    }

    for (int orbit = 0; orbit <= 1; orbit++) {
        if (orbit == 0 && !ctx->factors[fi].pp.fits_corners) continue;
        int c = pp_cost(&ctx->factors[fi].pp, orbit);
        if (c < 0) continue;
        int new_corners = used_corners + (orbit == 0 ? c : 0);
        int new_edges   = used_edges   + (orbit == 1 ? c : 0);
        if (new_corners > CCF_CORNER_COUNT) continue;
        if (new_edges   > CCF_EDGE_COUNT)   continue;
        ctx->factors[fi].orbit = orbit;
        ctx->factors[fi].cost  = c;
        place_rec(ctx, fi + 1, new_corners, new_edges);
    }
}

static void try_place(ArchCtx *ctx) {
    PlaceCtx pctx = {0};
    pctx.arch = ctx;
    for (int r = 0; r < ctx->cur_count; r++) {
        const CCFOrderSpec *spec = ctx->cur[r];
        for (int i = 0; i < spec->num_factors; i++) {
            PlacedFactor *pf = &pctx.factors[pctx.num_factors++];
            pf->pp      = spec->factors[i];
            pf->reg_idx = r;
        }
    }
    place_rec(&pctx, 0, 0, 0);
}

/* Returns 1 if b dominates a: same register count, every b order >= a order,
 * at least one strictly greater. */
static int dominates(const CCFArchitecture *b, const CCFArchitecture *a) {
    if (b->num_registers != a->num_registers) return 0;
    int strict = 0;
    for (int i = 0; i < a->num_registers; i++) {
        if (b->registers[i].order < a->registers[i].order) return 0;
        if (b->registers[i].order > a->registers[i].order) strict = 1;
    }
    return strict;
}

/* Returns 1 if any found architecture dominates every (cur_count+1)-register
 * candidate reachable from this branch — i.e. an architecture B exists with
 * the same register count whose first cur_count orders are all >= the current
 * partial's orders and whose last order is >= next_order (the best remaining
 * choice). Since specs are sorted descending, all later choices are <= next_order,
 * so the same B dominates them too; the caller can break out of the for loop. */
static int branch_dominated(const CCFResult *result,
                             const CCFOrderSpec * const *cur, int cur_count,
                             int next_order) {
    int target = cur_count + 1;
    for (int i = 0; i < result->count; i++) {
        const CCFArchitecture *b = &result->archs[i];
        if (b->num_registers != target) continue;
        int ok = 1;
        for (int j = 0; j < cur_count; j++)
            if (b->registers[j].order < cur[j]->order) { ok = 0; break; }
        if (ok && b->registers[cur_count].order >= next_order) return 1;
    }
    return 0;
}

static void arch_rec(ArchCtx *ctx, int from) {
    if (ctx->cur_count > 0)
        try_place(ctx);

    if (ctx->cur_count == CCF_MAX_REGISTERS)
        return;

    for (int i = from; i < ctx->spec_count; i++) {
        int new_min_edges = ctx->cur_min_edges + ctx->specs[i].min_edges;
        if (new_min_edges > CCF_EDGE_COUNT) continue;
        int new_min_total = ctx->cur_min_total + ctx->specs[i].min_corners;
        if (new_min_total > CCF_PIECE_COUNT) continue;
        ctx->cur[ctx->cur_count++] = &ctx->specs[i];
        ctx->cur_min_edges         = new_min_edges;
        ctx->cur_min_total         = new_min_total;
        arch_rec(ctx, i);
        ctx->cur_count--;
        ctx->cur_min_edges -= ctx->specs[i].min_edges;
        ctx->cur_min_total -= ctx->specs[i].min_corners;
        /* Break after recursion: if the (cur_count+1)-register arch we just
         * explored is dominated, all later specs (with smaller orders) would
         * also produce dominated (cur_count+1)-register archs. Only valid at
         * cur_count > 0; at the top level it would cut multi-register branches
         * that start with a small first register (e.g. [90,90]). */
        if (ctx->cur_count > 0 &&
            branch_dominated(ctx->result, ctx->cur, ctx->cur_count,
                             ctx->specs[i].order)) break;
    }
}

void ccf_find_architectures(const CCFOrderSpec *orders, const int order_count, CCFResult *result) {
    CCFOrderSpec *specs = malloc(order_count * sizeof *specs);
    if (!specs) return;
    memcpy(specs, orders, order_count * sizeof *specs);
    qsort(specs, order_count, sizeof *specs, cmp_spec_desc);

    ArchCtx ctx = {0};
    ctx.specs      = specs;
    ctx.spec_count = order_count;
    ctx.result     = result;
    arch_rec(&ctx, 0);
    free(specs);

    /* Final sweep: remove any architectures still dominated by another.
     * Swap-with-last to avoid shifting; re-examine index i after removal. */
    for (int i = 0; i < result->count; i++) {
        for (int j = 0; j < result->count; j++) {
            if (j == i) continue;
            if (dominates(&result->archs[j], &result->archs[i])) {
                result->archs[i] = result->archs[--result->count];
                i--;
                break;
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * CCF functions
 * -------------------------------------------------------------------------- */

void ccf_free(CCFResult *result) {
    free(result->archs);
    result->archs = NULL;
    result->count = 0;
    result->cap   = 0;
}

const CCFArchitecture *ccf_select(const CCFResult *result,
                                  const int *required_orders, int count,
                                  int bias_lo, int bias_hi) {
    const CCFArchitecture *best      = NULL;
    int                    best_bias = -1;
    int                    best_free = INT_MAX;

    for (int i = 0; i < result->count; i++) {
        const CCFArchitecture *a = &result->archs[i];
        if (a->num_registers < count) continue;

        /* Match each required order to a distinct register with exactly that
         * order. O(count * num_registers) — both are small. */
        int used[CCF_MAX_REGISTERS] = {0};
        int matched = 0;
        for (int r = 0; r < count; r++) {
            for (int j = 0; j < a->num_registers; j++) {
                if (!used[j] && a->registers[j].order == required_orders[r]) {
                    used[j] = 1;
                    matched++;
                    break;
                }
            }
        }
        if (matched < count) continue;

        /* Count registers whose order falls in the bias range. */
        int bias = 0;
        if (bias_lo > 0 || bias_hi > 0) {
            for (int j = 0; j < a->num_registers; j++) {
                int o = a->registers[j].order;
                if (o >= bias_lo && o <= bias_hi) bias++;
            }
        }

        int f = a->free_corners + a->free_edges;
        if (bias > best_bias || (bias == best_bias && f < best_free)) {
            best_bias = bias;
            best_free = f;
            best      = a;
        }
    }

    return best;
}

void ccf_dump(const CCFResult *result, FILE *fp) {
    fprintf(fp, "CCF: %d architecture(s)\n", result->count);
    for (int i = 0; i < result->count; i++) {
        const CCFArchitecture *a = &result->archs[i];
        fprintf(fp, "  [%d] %d register(s), free_corners=%d free_edges=%d\n",
                i, a->num_registers, a->free_corners, a->free_edges);
        for (int r = 0; r < a->num_registers; r++) {
            const CCFRegister *reg = &a->registers[r];
            fprintf(fp, "    reg[%d] order=%-4d cycles:", r, reg->order);
            for (int c = 0; c < reg->num_cycles; c++) {
                const CCFCycle *cy = &reg->cycles[c];
                fprintf(fp, " (%s len=%d ori=%+d ord=%d)",
                        cy->orbit == 0 ? "C" : "E",
                        cy->length, cy->net_orientation, cy->order);
            }
            fprintf(fp, "\n");
        }
    }
}

/* ============================================================================
 * ccf_run — generate architectures whose registers are each INDIVIDUALLY
 * reachable on the cube.
 *
 * The order-spec / place_rec path above balances the cube laws across the whole
 * architecture, so its registers are not independently reachable. But a
 * register's "add 1" sequence is applied on its own, so each register must be a
 * reachable cube state by itself, i.e. over its own pieces:
 *     (1) corner-perm parity == edge-perm parity   (mod 2)
 *     (2) sum of corner twists == 0                 (mod 3)
 *     (3) sum of edge flips   == 0                  (mod 2)
 *
 * ccf_run (a) enumerates reachable register "templates" (disjoint cycles obeying
 * 1-3, with their lcm order and piece cost), keeping the cheapest realisation of
 * each order, then (b) packs disjoint templates into architectures.
 * ========================================================================== */

static int igcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

/* Order contributed by one cycle: length * period / gcd(twist, period). */
static int cycle_order(int orbit, int length, int twist) {
    int period = (orbit == 0) ? CCF_CORNER_PERIOD : CCF_EDGE_PERIOD;
    return length * (period / igcd(twist, period));
}

typedef struct { uint8_t orbit, length; int8_t twist; } CandCycle;

/* The cheapest reachable register template found for each achievable order. */
typedef struct {
    CCFCycle cycles[CCF_MAX_CYCLES_PER_REG];
    int num_cycles, order, corner_cost, edge_cost;
    bool present;
} RegTemplate;

#define CCF_MAX_ORDER (MAX_3X3_ORDER + 1)

typedef struct {
    const CandCycle *cand;
    int ncand;
    RegTemplate *best;   /* indexed by order, size CCF_MAX_ORDER */
    CCFCycle cur[CCF_MAX_CYCLES_PER_REG];
    int ncur, cc, ec, cpar, epar, ctw, efl;
} TmplCtx;

/* Record the current cycle set if it is a reachable, order>1 register, keeping
 * the cheapest (fewest pieces, then fewest cycles) realisation per order. */
static void tmpl_emit(TmplCtx *t) {
    if (t->ncur == 0) return;
    if (t->cpar != t->epar) return;          /* law 1 */
    if (((t->ctw % 3) + 3) % 3 != 0) return; /* law 2 */
    if ((t->efl & 1) != 0) return;           /* law 3 */

    int order = 1;
    for (int i = 0; i < t->ncur; i++)
        order = order / igcd(order, t->cur[i].order) * t->cur[i].order;
    if (order <= 1 || order >= CCF_MAX_ORDER) return;

    RegTemplate *b = &t->best[order];
    int pieces = t->cc + t->ec;
    if (b->present) {
        int bp = b->corner_cost + b->edge_cost;
        if (pieces > bp || (pieces == bp && t->ncur >= b->num_cycles)) return;
    }
    b->present = true;
    b->num_cycles = t->ncur;
    b->order = order;
    b->corner_cost = t->cc;
    b->edge_cost = t->ec;
    for (int i = 0; i < t->ncur; i++) b->cycles[i] = t->cur[i];
}

/* DFS over candidate cycles (chosen by non-decreasing index to avoid permuted
 * duplicates), respecting the 8-corner / 12-edge budget. */
static void tmpl_rec(TmplCtx *t, int start) {
    tmpl_emit(t);
    if (t->ncur == CCF_MAX_CYCLES_PER_REG) return;

    for (int i = start; i < t->ncand; i++) {
        const CandCycle *c = &t->cand[i];
        int nc = t->cc + (c->orbit == 0 ? c->length : 0);
        int ne = t->ec + (c->orbit == 1 ? c->length : 0);
        if (nc > CCF_CORNER_COUNT || ne > CCF_EDGE_COUNT) continue;

        CCFCycle *cy = &t->cur[t->ncur++];
        cy->orbit = c->orbit;
        cy->length = c->length;
        cy->net_orientation = c->twist;
        cy->order = cycle_order(c->orbit, c->length, c->twist);
        t->cc = nc; t->ec = ne;
        if (c->orbit == 0) { t->cpar ^= (c->length - 1) & 1; t->ctw += c->twist; }
        else               { t->epar ^= (c->length - 1) & 1; t->efl += c->twist; }

        tmpl_rec(t, i); /* i (not i+1): allow repeated cycle types on fresh pieces */

        t->ncur--;
        t->cc = nc - (c->orbit == 0 ? c->length : 0);
        t->ec = ne - (c->orbit == 1 ? c->length : 0);
        if (c->orbit == 0) { t->cpar ^= (c->length - 1) & 1; t->ctw -= c->twist; }
        else               { t->epar ^= (c->length - 1) & 1; t->efl -= c->twist; }
    }
}

/* Pack disjoint templates (sorted by order, descending) into architectures. */
typedef struct {
    const RegTemplate **tmpl; /* distinct-order templates, order-descending */
    int ntmpl;
    CCFResult *out;
    const RegTemplate *cur[CCF_MAX_REGISTERS];
    int ncur, cc, ec;
    int emitted;
} PackCtx;

#define CCF_MAX_ARCHS 20000

static void pack_emit(PackCtx *p) {
    if (p->ncur == 0 || p->emitted >= CCF_MAX_ARCHS) return;

    CCFArchitecture arch = {0};
    arch.num_registers = p->ncur;
    arch.free_corners = CCF_CORNER_COUNT - p->cc;
    arch.free_edges = CCF_EDGE_COUNT - p->ec;
    for (int r = 0; r < p->ncur; r++) {
        const RegTemplate *tm = p->cur[r];
        CCFRegister *reg = &arch.registers[r];
        reg->order = tm->order;
        reg->num_cycles = tm->num_cycles;
        for (int c = 0; c < tm->num_cycles; c++) reg->cycles[c] = tm->cycles[c];
    }
    if (result_append(p->out, &arch)) p->emitted++;
}

static void pack_rec(PackCtx *p, int start) {
    pack_emit(p);
    if (p->ncur == CCF_MAX_REGISTERS || p->emitted >= CCF_MAX_ARCHS) return;

    for (int i = start; i < p->ntmpl; i++) {
        const RegTemplate *tm = p->tmpl[i];
        if (p->cc + tm->corner_cost > CCF_CORNER_COUNT) continue;
        if (p->ec + tm->edge_cost > CCF_EDGE_COUNT) continue;
        p->cur[p->ncur++] = tm;
        p->cc += tm->corner_cost;
        p->ec += tm->edge_cost;
        pack_rec(p, i + 1);
        p->ncur--;
        p->cc -= tm->corner_cost;
        p->ec -= tm->edge_cost;
        if (p->emitted >= CCF_MAX_ARCHS) return;
    }
}

static int cmp_tmpl_order_desc(const void *a, const void *b) {
    return (*(const RegTemplate *const *)b)->order - (*(const RegTemplate *const *)a)->order;
}

void ccf_run(CCFResult *out) {
    out->archs = NULL; out->count = 0; out->cap = 0;

    /* Candidate cycles: corners L=1..8, edges L=1..12; twist 0..period-1.
     * Length-1 with zero twist is a no-op, so it is skipped. */
    CandCycle cand[64];
    int ncand = 0;
    for (int L = 1; L <= CCF_CORNER_COUNT; L++)
        for (int w = 0; w < CCF_CORNER_PERIOD; w++) {
            if (L == 1 && w == 0) continue;
            cand[ncand++] = (CandCycle){0, (uint8_t)L, (int8_t)w};
        }
    for (int L = 1; L <= CCF_EDGE_COUNT; L++)
        for (int w = 0; w < CCF_EDGE_PERIOD; w++) {
            if (L == 1 && w == 0) continue;
            cand[ncand++] = (CandCycle){1, (uint8_t)L, (int8_t)w};
        }

    RegTemplate *best = calloc(CCF_MAX_ORDER, sizeof *best);
    if (!best) return;

    TmplCtx t = {0};
    t.cand = cand; t.ncand = ncand; t.best = best;
    tmpl_rec(&t, 0);

    /* Collect distinct-order templates, order-descending. */
    const RegTemplate **list = malloc(CCF_MAX_ORDER * sizeof *list);
    int nlist = 0;
    if (list) {
        for (int o = 0; o < CCF_MAX_ORDER; o++)
            if (best[o].present) list[nlist++] = &best[o];
        qsort(list, nlist, sizeof *list, cmp_tmpl_order_desc);

        PackCtx p = {0};
        p.tmpl = list; p.ntmpl = nlist; p.out = out;
        pack_rec(&p, 0);
        free(list);
    }

    free(best);

    /* Drop architectures dominated by another with the same register count. */
    for (int i = 0; i < out->count; i++)
        for (int j = 0; j < out->count; j++)
            if (j != i && dominates(&out->archs[j], &out->archs[i])) {
                out->archs[i] = out->archs[--out->count];
                i--;
                break;
            }
}
