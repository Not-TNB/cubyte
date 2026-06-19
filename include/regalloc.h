#ifndef ARMV8_37_REGALLOC_H
#define ARMV8_37_REGALLOC_H

#include "cube.h"
#include "interference.h"

/* Register table
 * Index 0 is the physical R0 algorithm.
 * Index 1 is the codegen scratch register.
 * User variables may use R0 when RegTable.r0_reserved is false.
 */

typedef struct {
    char     *algorithm;  // SiGN string
    int       order;      // K_i: period of the register algorithm
    CycleSet  cycles;     // support of the register algorithm
    int       index;      // i
} RegEntry;

typedef struct {
    RegEntry *regs;
    int       count;
    int       cap;
    bool      r0_reserved;
} RegTable;

/* Initialise table and pre-populate R0 plus the reserved scratch register R1.
 * cube_init() must be called before this. temp_required_order is the largest
 * order any int variable in the program needs; the scratch register must be at
 * least that wide because every variable add/sub routes through it and codegen
 * requires temp order >= source order. Pass 0 for the legacy order-3 scratch. */
void regalloc_init(RegTable *table, int temp_required_order);
void regalloc_free(RegTable *table);

/* Find an algorithm whose cycle set is disjoint from `forbidden`, append it to
 * the table, and return its index. Unconstrained/order-3 requests use the
 * Kociemba-backed CCS generator first; other orders fall back to bounded IDA*. */
int regalloc_find_and_add(RegTable *table, CycleSet forbidden, int required_order);

/* Colour every int variable in ig.
 * Visits IG nodes in deterministic order: highest degree first, ties broken
 * alphabetically. For each node, builds a forbidden CycleSet from reserved
 * registers plus every already-coloured neighbour's cycles, then reuses the
 * first existing non-scratch register whose cycles are disjoint from forbidden,
 * or calls regalloc_find_and_add to generate a new one.
 *
 * Returns a heap-allocated int[env->count] where result[var_index] is the
 * RegTable index assigned to that variable, or -1 for R0 / alg variables.
 * Physical R0 is reserved only when table->r0_reserved is true.
 * Caller frees the returned array.
 *
 * die(EXIT_CODE_REGALLOC) if no disjoint register can be generated.
 * die(EXIT_CODE_INTERNAL) if the post-colouring disjointness invariant fails. */
int *regalloc_run(RegTable *table, const InterferenceGraph *ig);

/* Print --dump-regs output to out.
 * One line per variable (name-sorted): "var -> Ri  alg=\"...\" K=n C={...}" */
void regalloc_dump(const RegTable *table, const InterferenceGraph *ig,
                   const int *coloring, FILE *out);

#endif /* ARMV8_37_REGALLOC_H */
