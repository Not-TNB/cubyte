#ifndef CUBIT_CCF_H
#define CUBIT_CCF_H

#include <stdint.h>
#include <stdio.h>

/*
 * Cycle Combination Finder (CCF)
 *
 * Determines all non-redundant multi-register architectures achievable on the
 * 3x3 cube.  Runs once at startup (or is loaded from cache) and its output
 * drives architecture selection and the CCS.
 *
 * Three steps:
 *   1. Prime power discovery   — which p^k fit in corners / edges?
 *   2. Composite order enum    — which single-register orders are achievable?
 *   3. Architecture search     — which multi-register combos are non-redundant?
 */

/* 3x3 cube constants. */
#define CCF_CORNER_COUNT   8
#define CCF_EDGE_COUNT    12
#define CCF_CORNER_PERIOD  3   /* orientation period for corners */
#define CCF_EDGE_PERIOD    2   /* orientation period for edges   */
#define CCF_PIECE_COUNT   (CCF_CORNER_COUNT + CCF_EDGE_COUNT)  /* 20 */

/* Exact number of prime powers that fit on the 3x3: 1,2,3,4,5,7,8,9,11,16. */
#define CCF_MAX_PRIME_POWERS 10

/* --------------------------------------------------------------------------
 * Abstract cycle description (no specific piece indices — CCF works purely
 * at the level of counts and orientations).
 * -------------------------------------------------------------------------- */
typedef struct {
    uint8_t orbit;           /* 0 = corners (period 3), 1 = edges (period 2) */
    uint8_t length;          /* number of pieces in cycle                     */
    int8_t  net_orientation; /* sum of orientation deltas, mod period          */
    int     order;           /* length * (period / gcd(net_orientation, period)) */
} CCFCycle;

/* One register within an architecture: a cycle combination (set of abstract
 * cycles whose orders combine via LCM to give the register's total order). */
#define CCF_MAX_CYCLES_PER_REG 10
typedef struct {
    CCFCycle cycles[CCF_MAX_CYCLES_PER_REG];
    int      num_cycles;
    int      order;          /* LCM of all cycle orders */
} CCFRegister;

/* A complete multi-register architecture.
 * registers[] are sorted descending by order (to avoid duplicates). */
#define CCF_MAX_REGISTERS 8
typedef struct {
    CCFRegister registers[CCF_MAX_REGISTERS];
    int         num_registers;
    int         free_corners; /* unassigned corners available for parity/orientation */
    int         free_edges;   /* unassigned edges   available for parity/orientation */
} CCFArchitecture;

/* The full CCF output: a list of non-redundant architectures. */
typedef struct {
    CCFArchitecture *archs;
    int              count;
    int              cap;
} CCFResult;

/* --------------------------------------------------------------------------
 * Intermediate types exposed for step-level testing.
 * -------------------------------------------------------------------------- */

/* A single prime power p^k and its piece-budget requirements.
 *
 * p^k fits an orbit if there exists m <= k such that the orbit has at least
 * p^m pieces and p^(k-m) divides the orbit's orientation period.
 * min_pieces = p^m (0 when m = 0, i.e. a pure-orientation cycle). */
typedef struct {
    int p;            /* prime base                                         */
    int k;            /* exponent: represents p^k                           */
    int value;        /* p^k                                                */
    int min_pieces;   /* minimum pieces consumed (0 when pure-orientation)  */
    int fits_corners; /* non-zero if achievable in the corner orbit         */
    /* Every prime power that fits the 3x3 at all fits in the edge orbit;
     * a separate fits_edges flag is therefore not stored. */
} CCFPrimePower;

/*
 * All prime powers that fit on the 3x3, in ascending order of value.
 * min_pieces is the global minimum over all valid orbit placements (the
 * minimum m such that p^m pieces suffice with orientation making up the rest).
 * Entry 0 is the trivial p^0 = 1 identity used as a multiplicative base in
 * the composite-order recursion.
 */
static const CCFPrimePower CCF_PRIME_POWERS[CCF_MAX_PRIME_POWERS] = {
/*  p   k  value  min_pieces  fits_corners */
  { 2,  1,     2,          0,           1 }, /* pure-orientation: 2|2 (edge period) */
  { 3,  1,     3,          0,           1 }, /* pure-orientation: 3|3 (corner period) */
  { 2,  2,     4,          2,           1 }, /* edges: m=1 → 2 pieces, 2|2          */
  { 5,  1,     5,          5,           1 }, /* large prime: 5 pieces, fits corners  */
  { 7,  1,     7,          7,           1 }, /* large prime: 7 pieces, fits corners  */
  { 2,  3,     8,          4,           1 }, /* edges: m=2 → 4 pieces, 2|2          */
  { 3,  2,     9,          3,           1 }, /* corners: m=1 → 3 pieces, 3|3        */
  {11,  1,    11,         11,           0 }, /* edges only: 11 > 8 corner slots      */
  { 2,  4,    16,          8,           0 }, /* edges only: no 2^k divides 3         */
};

/* A single achievable register order with its prime-power decomposition. */
#define CCF_MAX_PRIME_FACTORS 6
typedef struct {
    int           order;
    CCFPrimePower factors[CCF_MAX_PRIME_FACTORS];
    int           num_factors;
    int           min_corners; /* sum of min_pieces across all factors (total piece budget lower bound) */
    int           min_edges;   /* sum of min_pieces for edge-only factors (fits_corners==0) */
} CCFOrderSpec;

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/*
 * Run the full CCF pipeline and write results into *out.
 * cube_init() and piece_cube_init() must already have been called.
 * Allocates out->archs; caller must call ccf_free() when done.
 */
void ccf_run(CCFResult *out);

/* Release all memory owned by *result and zero it. */
void ccf_free(CCFResult *result);


/*
 * Step 2: given the prime powers from step 1, fill out[] with all achievable
 * composite register orders and their prime-power decompositions.
 * Orders are pruned when their combined minimum piece count exceeds
 * CCF_PIECE_COUNT.  Returns the number written; cap is the buffer size.
 */
int ccf_order_specs(const CCFPrimePower *powers, int power_count,
                    CCFOrderSpec *out, int cap);

/*
 * Step 3: given the order specs, fill *result with all non-redundant
 * multi-register architectures.  Architectures are generated in descending
 * register-order to avoid duplicates, and redundant ones are pruned during
 * search.  Allocates result->archs; caller must call ccf_free() when done.
 */
void ccf_find_architectures(const CCFOrderSpec *orders, int order_count,
                             CCFResult *result);

/*
 * Print a human-readable summary of *result to fp.
 */
void ccf_dump(const CCFResult *result, FILE *fp);

/*
 * Select the best architecture from *result for a given set of required
 * register orders (one entry per program variable that needs a register).
 * Returns a pointer into result->archs, or NULL if no architecture satisfies
 * all constraints.
 *
 * bias_lo/bias_hi define an inclusive order range to favour: among candidates
 * that satisfy criteria 1-2, the architecture with the most registers whose
 * order falls in [bias_lo, bias_hi] wins. Pass bias_lo=0, bias_hi=0 to skip
 * this tiebreak and fall through to criterion 3 directly.
 *
 * Selection criteria (in priority order):
 *   1. Enough registers to cover all required orders.
 *   2. Each register's order exactly matches one required order.
 *   3. Maximise registers with order in [bias_lo, bias_hi]  (if range given).
 *   4. Minimise free_corners + free_edges.
 */
const CCFArchitecture *ccf_select(const CCFResult *result,
                                  const int *required_orders, int count,
                                  int bias_lo, int bias_hi);

#endif /* CUBIT_CCF_H */
