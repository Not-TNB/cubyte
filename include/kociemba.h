#ifndef ARMV8_37_KOCIEMBA_H
#define ARMV8_37_KOCIEMBA_H

#include <stdbool.h>
#include "cube.h"
#include "alg.h"

/*
 * Takes a move sequence in SiGN notation (e.g. "U R U' R'") and returns a
 * malloc'd, equivalent, generally-shorter sequence via Kociemba's two-phase
 * algorithm. Caller must free() the result.
 *
 * Returns NULL on failure (malformed input, solver error). Caller should fall
 * back to the original algorithm rather than silently dropping output.
 * Returns a malloc'd empty string "" for an already-solved (identity) input.
 */
char *kociemba_shorten(const char *raw_alg);

/*
 * Find a face-turn algorithm that produces *target from the identity cube,
 * via Kociemba's two-phase solver (any reachable state, <= ~20 moves — no
 * search-depth ceiling). On success fills *out (caller must alg_free) and
 * returns true. For the identity target, *out is the empty alg.
 *
 * Returns false if *target is unreachable (bad parity/orientation) or the
 * solver fails. cube_init() is called lazily on first use.
 */
bool kociemba_solve_state(const CubeState *target, Alg *out);

#endif /* ARMV8_37_KOCIEMBA_H */
