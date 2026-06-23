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

static int dominates(const CCFArchitecture *b, const CCFArchitecture *a) {
    if (b->num_registers != a->num_registers) return 0;
    int strict = 0;
    for (int i = 0; i < a->num_registers; i++) {
        if (b->registers[i].order < a->registers[i].order) return 0;
        if (b->registers[i].order > a->registers[i].order) strict = 1;
    }
    return strict;
}

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

static void build_cand_cycles(CandCycle *cand, int *ncand) {
    *ncand = 0;
    for (int L = 1; L <= CCF_CORNER_COUNT; L++)
        for (int w = 0; w < CCF_CORNER_PERIOD; w++) {
            if (L == 1 && w == 0) continue;
            cand[(*ncand)++] = (CandCycle){0, (uint8_t)L, (int8_t)w};
        }
    for (int L = 1; L <= CCF_EDGE_COUNT; L++)
        for (int w = 0; w < CCF_EDGE_PERIOD; w++) {
            if (L == 1 && w == 0) continue;
            cand[(*ncand)++] = (CandCycle){1, (uint8_t)L, (int8_t)w};
        }
}

void ccf_run(CCFResult *out) {
    out->archs = NULL; out->count = 0; out->cap = 0;

    CandCycle cand[64];
    int ncand = 0;
    build_cand_cycles(cand, &ncand);

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

static int cmp_tmpl_order_asc(const void *a, const void *b) {
    return (*(const RegTemplate *const *)a)->order - (*(const RegTemplate *const *)b)->order;
}

int ccf_enumerate_templates(CCFTemplate *out, int cap) {
    CandCycle cand[64];
    int ncand = 0;
    build_cand_cycles(cand, &ncand);

    RegTemplate *best = calloc(CCF_MAX_ORDER, sizeof *best);
    if (!best) return 0;

    TmplCtx t = {0};
    t.cand = cand; t.ncand = ncand; t.best = best;
    tmpl_rec(&t, 0);

    const RegTemplate **list = malloc(CCF_MAX_ORDER * sizeof *list);
    int nlist = 0;
    if (list) {
        for (int o = 0; o < CCF_MAX_ORDER; o++)
            if (best[o].present) list[nlist++] = &best[o];
        qsort(list, nlist, sizeof *list, cmp_tmpl_order_asc);
    }

    int written = 0;
    if (list) {
        for (int i = 0; i < nlist && written < cap; i++) {
            const RegTemplate *tm = list[i];
            CCFTemplate *o = &out[written++];
            o->order       = tm->order;
            o->num_cycles  = tm->num_cycles;
            o->corner_cost = tm->corner_cost;
            o->edge_cost   = tm->edge_cost;
            for (int c = 0; c < tm->num_cycles; c++)
                o->cycles[c] = tm->cycles[c];
        }
        free(list);
    }

    free(best);
    return written;
}
