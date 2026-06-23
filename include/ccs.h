#ifndef CUBIT_CCS_H
#define CUBIT_CCS_H

/*
 * Cycle Combination Solver (CCS)
 *
 * Given a CCFArchitecture (abstract register specifications), finds a
 * near-optimal concrete Rubik's-cube algorithm implementing each register's
 * "add 1" operation.  All other powers are derived externally by repetition.
 *
 * Pipeline for each piece assignment:
 *
 *   Step 1 — Piece assignment  (ccs_enumerate_assignments)
 *     Choose disjoint piece subsets from the corner/edge pools, one subset
 *     per cycle, matching each cycle's orbit and length.  Enforcing disjointness
 *     by construction guarantees that all register algorithms commute.
 *
 *   Step 2 — Build target state  (inside ccs_find_alg → build_target)
 *     Map the piece assignment to a concrete CubeState T.  Any algorithm
 *     carrying identity → T implements the desired cycle structure.
 *     Currently only net_orientation = 0 is supported (all ori_deltas = 0).
 *
 *   Step 3 — Algorithm search  (ccs_find_alg → kociemba_solve_state)
 *     Delegates to Kociemba's two-phase solver, which finds a near-optimal
 *     solution (≤20 HTM) in milliseconds using pre-built pruning tables.
 *
 *   Step 4 — Collect results
 *     Successful architectures (all registers solved) are appended to the
 *     CCSResult.  ccs_best() selects the one with fewest total moves.
 */

#include "ccf.h"
#include "cube.h"
#include "alg.h"
#include "piece.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

/* Maximum number of pieces in a single cycle (12 edges on the 3×3). */
#define CCS_MAX_CYCLE_LEN 12

/* --------------------------------------------------------------------------
 * CCSCycle — one abstract cycle + concrete piece assignment.
 *
 * pieces[0..abstract.length-1] are the assigned PieceLabels.
 * ori_deltas[i] is the orientation shift that pieces[i] accumulates per step.
 * The sum of ori_deltas over the cycle must equal abstract.net_orientation
 * (mod the orbit's period).  For net_orientation = 0, all deltas are zero.
 * -------------------------------------------------------------------------- */
typedef struct {
    CCFCycle   abstract;
    PieceLabel pieces[CCS_MAX_CYCLE_LEN];
    int8_t     ori_deltas[CCS_MAX_CYCLE_LEN];
} CCSCycle;

/* --------------------------------------------------------------------------
 * CCSRegister — one register with its concrete algorithm.
 * -------------------------------------------------------------------------- */
typedef struct {
    CCFRegister abstract;
    CCSCycle    cycles[CCF_MAX_CYCLES_PER_REG];
    Alg         alg;
    CycleSet    cycleset; /* set of all pieces displaced by alg */
} CCSRegister;

/* --------------------------------------------------------------------------
 * CCSArchitecture — all registers for one concrete architecture.
 * -------------------------------------------------------------------------- */
typedef struct {
    const CCFArchitecture *abstract;
    CCSRegister            regs[CCF_MAX_REGISTERS];
    int                    num_regs;
    CycleSet               free_pieces; /* pieces not assigned to any register */
} CCSArchitecture;

/* --------------------------------------------------------------------------
 * CCSResult — a growable list of valid concrete architectures.
 * -------------------------------------------------------------------------- */
typedef struct {
    CCSArchitecture *archs;
    int              count;
    int              cap;
} CCSResult;

/* ==========================================================================
 * Public API
 * ========================================================================== */

/*
 * Run the full CCS pipeline for *arch.
 * Enumerates all piece assignments, finds the shortest algorithm for each
 * register in each assignment, and appends successful architectures to *out.
 * cube_init() must have been called first.
 * Allocates out->archs; caller must call ccs_free() when done.
 */
void ccs_solve(const CCFArchitecture *arch, CCSResult *out);

/* Release all memory owned by *result and zero the struct. */
void ccs_free(CCSResult *result);

/*
 * Return the architecture in *result with the fewest total algorithm moves,
 * or NULL if *result is empty.
 */
const CCSArchitecture *ccs_best(const CCSResult *result);

/*
 * Verify a concrete architecture:
 *   1. compute_order(alg) == abstract.order  for every register
 *   2. cycleset_from_alg(alg) == reg.cycleset  for every register
 *   3. all register cyclesets are pairwise disjoint
 *   4. free_pieces == complement of the union of all cyclesets
 * Returns true iff all four checks pass.
 */
bool ccs_verify(const CCSArchitecture *arch);

/* Print a human-readable summary of *arch to fp. */
void ccs_dump(const CCSArchitecture *arch, FILE *fp);

/* ==========================================================================
 * Step-level API — exposed for testing
 * ========================================================================== */

/*
 * Callback invoked by ccs_enumerate_assignments for each valid piece assignment.
 * cycles[r][c] has its pieces[] field filled; abstract and ori_deltas are not
 * set here.
 *
 * Return true to keep enumerating, or false to stop early. Because all piece
 * assignments of a given orbit/length are equivalent under cube symmetry, a
 * caller that just needs one working assignment should return false after the
 * first success — this avoids the combinatorial blow-up of full enumeration.
 */
typedef bool (*CCSAssignCB)(
    CCSCycle cycles[CCF_MAX_REGISTERS][CCF_MAX_CYCLES_PER_REG],
    int num_regs, void *ctx);

/*
 * Enumerate ways to assign disjoint pieces to the cycles of *arch, calling
 * cb(cycles, num_regs, ctx) for each. Stops early once cb returns false.
 * Returns the number of assignments passed to cb (including the stopping one).
 *
 * forbidden: pieces pre-marked as unavailable (e.g. claimed by other registers).
 * Pass CYCLESET_EMPTY to allow all pieces.
 */
int ccs_enumerate_assignments(const CCFArchitecture *arch,
                              CycleSet forbidden,
                              CCSAssignCB cb, void *ctx);

/*
 * Find an algorithm implementing *reg's "add 1" operation.
 * Builds a target CubeState from reg's piece assignment and orientation deltas,
 * then uses Kociemba's two-phase solver to find a face-turn sequence producing
 * it (any reachable state, no search-depth ceiling).
 * On success: fills *out with a heap-allocated Alg; caller must alg_free() it.
 * On failure (unreachable target / wrong order): returns false; *out untouched.
 */
bool ccs_find_alg(const CCSRegister *reg, Alg *out);

#endif /* CUBIT_CCS_H */
