#ifndef CUBIT_CUBE_H
#define CUBIT_CUBE_H

/* -----------------------------------------------------------------------------
 * FACELET NUMBERING (48 non-centre facelets, 8 per face)
 * -----------------------------------------------------------------------------
 *
 * Face idx ranges:
 *   U :  0 –  7   (from above, front face = F)
 *   D :  8 – 15   (from below, front face = F)
 *   L : 16 – 23   (head-on, top = U direction)
 *   R : 24 – 31   (head-on, top = U)
 *   F : 32 – 39   (head-on, top = U)
 *   B : 40 – 47   (head-on, top = U)
 *
 * Within every face the 8 facelets are laid out row-major from the top-left
 * corner of that face in its stated viewing orientation:
 *
 *   position 0  1  2
 *            3  .  4      ( . = centre sticker, not indexed )
 *            5  6  7
 *
 * Net diagram (cube unfolded, centres marked with face letter):
 *
 *              +--------+
 *              | 0  1  2|
 *              | 3  U  4|
 *              | 5  6  7|
 *   +----------+--------+----------+----------+
 *   | 16 17 18 |32 33 34| 24 25 26 | 40 41 42 |
 *   | 19  L 20 |35  F 36| 27  R 28 | 43  B 44 |
 *   | 21 22 23 |37 38 39| 29 30 31 | 45 46 47 |
 *   +----------+--------+----------+----------+
 *              | 8  9 10|
 *              |11  D 12|
 *              |13 14 15|
 *              +--------+
 *
 * -----------------------------------------------------------------------------
 * PIECE LABELS
 * -----------------------------------------------------------------------------
 *
 * 20 cubies: 12 edges + 8 corners. Listed in the order of the PieceLabel enum
 * below; used as bit positions in CycleSet.
 *
 * Edges (12): UF UB UL UR  DF DB DL DR  FL FR BL BR
 * Corners (8): UFL UFR UBL UBR  DFL DFR DBL DBR
 */

#include <stdint.h>
#include <stdbool.h>
#include "piece.h"

#define FACELET_COUNT 48
#define MAX_3X3_ORDER 1260

/* --- FORWARD DECS --- */
typedef struct Alg Alg;

/* --- CUBE STATE ---
 * state[i] = idx of the facelet currently occupying slot i.
 * If in identity, state[i] == i forall i in [0, 47].
 * Orientation of the cube is fixed; state encodes every non-centre facelet's current position.
 */
typedef struct {
    uint8_t state[FACELET_COUNT];
} CubeState;

/* --- Face indices --- */
typedef enum {
    FACE_U = 0,
    FACE_D = 1,
    FACE_L = 2,
    FACE_R = 3,
    FACE_F = 4,
    FACE_B = 5,
    FACE_COUNT = 6,
} Face;

/* --- CycleSet ---
 * A bitmask over PieceLabel.
 * Bit i is set iff the piece with enum value i is displaced by the algorithm
 * (i.e. at least one of its facelets occupies a different slot after applying
 * the algorithm to the identity cube).
 *
 * A cubie that is twisted or flipped in place counts as displaced because its
 * facelets have moved to different positions on the same cubie's slots.
 *
 * disjoint(a, b)  iff  (a & b) == 0
 */
typedef uint32_t CycleSet;

#define CYCLESET_EMPTY ((CycleSet)0u)
#define CYCLESET_FULL  ((CycleSet)((1u << PC_COUNT) - 1u))

static inline bool cycleset_disjoint(CycleSet a, CycleSet b) {
    return (a & b) == 0;
}

static inline CycleSet cycleset_union(CycleSet a, CycleSet b) {
    return a | b;
}

/* --- Cube initialiser ---
 * Initialise the module.  Must be called once before any other cube_* function.
 * Derives all 18 move permutation tables (X2 and X' are composed of the six
 * handwritten clockwise quarter-turn tables; see cube.c).
 *
 * Also builds the static facelet_to_piece[48] table used by cycleset_from_alg.
 */
void cube_init(void);

/* set s to the identity (state[i] = i). */
void cube_identity(CubeState *s);

/* copy src into dst. */
void cube_copy(CubeState *dst, const CubeState *src);

/* iff s is the identity state. */
bool cube_is_identity(const CubeState *s);

/* iff a and b represent the same cube state. */
bool cube_equal(const CubeState *a, const CubeState *b);

/* --- Applying moves ---
 * cube_apply_move applies a single move to *s in place.
 *   face:          one of the FACE_* constants
 *   quarter_turns: 1 = CW, 2 = 180°, 3 = CCW  (values outside 1..3 are UB)
 *
 * cube_apply_sequence applies every move in *a to *s in order (left-to-right).
 */
void cube_apply_move(CubeState *s, Face face, int quarter_turns);
void cube_apply_sequence(CubeState *s, const Alg *a);

/* --- Order computation ---
 * Returns the smallest n > 0 such that applying *a to the identity n times
 * yields the identity.
 *
 * The maximum possible order for a 3×3 Rubik's cube position is 1260.
 * If the iteration exceeds 1260 without returning to identity, the function
 * calls die(5, "internal", -1, "compute_order exceeded 1260: table bug") —
 * this indicates a corrupted move table and should never fire in correct code.
 */
int compute_order(const Alg *a);

/* --- Cycle set ---
 * Returns the CycleSet of algorithm *a: the set of all cubies that are
 * displaced (have at least one facelet in a different slot) when *a is applied
 * to the identity cube.
 *
 * apply *a once to cube_identity(), then for each facelet i,
 * if state[i] != i, mark facelet_to_piece[i] as displaced.
 */
CycleSet cycleset_from_alg(const Alg *a);

/* --- Facelet-to-piece table (read-only after cube_init) ---
 * facelet_to_piece[i] is the PieceLabel of the cubie whose sticker occupies
 * slot i in the solved cube.  Derived from the facelet numbering scheme above.
 *
 * This table is exposed so that the register allocator and emulator can test
 * individual facelets without going through the full cycleset_from_alg path.
 *
 * Contents (derived from the net diagram):
 *
 *   Slot  0: UBL corner (U face, top-left when viewed from above)
 *   Slot  1: UB  edge   (U face, top-middle)
 *   Slot  2: UBR corner (U face, top-right)
 *   Slot  3: UL  edge   (U face, mid-left)
 *   Slot  4: UR  edge   (U face, mid-right)
 *   Slot  5: UFL corner (U face, bot-left)
 *   Slot  6: UF  edge   (U face, bot-middle)
 *   Slot  7: UFR corner (U face, bot-right)
 *
 *   Slot  8: DFL corner
 *   Slot  9: DF  edge
 *   Slot 10: DFR corner
 *   Slot 11: DL  edge
 *   Slot 12: DR  edge
 *   Slot 13: DBL corner
 *   Slot 14: DB  edge
 *   Slot 15: DBR corner
 *
 *   Slot 16: UBL corner (L face, top-left when viewed head-on)
 *   Slot 17: UL  edge
 *   Slot 18: UFL corner
 *   Slot 19: BL  edge
 *   Slot 20: FL  edge
 *   Slot 21: DBL corner
 *   Slot 22: DL  edge
 *   Slot 23: DFL corner
 *
 *   Slot 24: UFR corner (R face, top-left when viewed head-on)
 *   Slot 25: UR  edge
 *   Slot 26: UBR corner
 *   Slot 27: FR  edge
 *   Slot 28: BR  edge
 *   Slot 29: DFR corner
 *   Slot 30: DR  edge
 *   Slot 31: DBR corner
 *
 *   Slot 32: UFL corner (F face, top-left when viewed head-on)
 *   Slot 33: UF  edge
 *   Slot 34: UFR corner
 *   Slot 35: FL  edge
 *   Slot 36: FR  edge
 *   Slot 37: DFL corner
 *   Slot 38: DF  edge
 *   Slot 39: DFR corner
 *
 *   Slot 40: UBR corner (B face, top-left when viewed head-on with U above)
 *   Slot 41: UB  edge
 *   Slot 42: UBL corner
 *   Slot 43: BR  edge
 *   Slot 44: BL  edge
 *   Slot 45: DBR corner
 *   Slot 46: DB  edge
 *   Slot 47: DBL corner
 */
extern PieceLabel facelet_to_piece[FACELET_COUNT]; /* written by cube_init(); treat as read-only after */

/* --- Print / Debug --- */

void cube_print_state(const CubeState *s);
void cycleset_print(CycleSet cs);

#endif /* CUBIT_CUBE_H */
