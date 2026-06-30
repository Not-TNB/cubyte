#ifndef CUBE4_H
#define CUBE4_H

/* -----------------------------------------------------------------------------
 * FACELET NUMBERING (96 facelets, 16 per face)
 * -----------------------------------------------------------------------------
 *
 * Face index ranges (same Face enum as cube3.h):
 *   U :  0 – 15   (viewed from above, F toward you at bottom)
 *   D : 16 – 31   (viewed from below, F toward you at top)
 *   L : 32 – 47   (viewed head-on, U above, B on left, F on right)
 *   R : 48 – 63   (viewed head-on, U above, F on left, B on right)
 *   F : 64 – 79   (viewed head-on, U above, L on left, R on right)
 *   B : 80 – 95   (viewed from outside back, U above, R on left, L on right)
 *
 * Face offset = face_index * 16.
 *
 * Within every face, the 16 facelets are laid out row-major from the top-left
 * corner of that face in its stated viewing orientation:
 *
 *    0   1   2   3
 *    4   5   6   7
 *    8   9  10  11
 *   12  13  14  15
 *
 *   Corners  : local slots  0,  3, 12, 15
 *   Wings    : local slots  1,  2,  4,  7,  8, 11, 13, 14
 *   X-centres: local slots  5,  6,  9, 10
 *
 * Net diagram (U sits directly above F; D directly below F):
 *
 *                     +----+----+----+----+
 *                     |  0 |  1 |  2 |  3 |
 *                     +----+----+----+----+
 *                     |  4 |  5 |  6 |  7 |
 *                     +----+----+----+----+
 *                     |  8 |  9 | 10 | 11 |
 *                     +----+----+----+----+
 *                     | 12 | 13 | 14 | 15 |
 * +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 * | 32 | 33 | 34 | 35 | 64 | 65 | 66 | 67 | 48 | 49 | 50 | 51 | 80 | 81 | 82 | 83 |
 * +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 * | 36 | 37 | 38 | 39 | 68 | 69 | 70 | 71 | 52 | 53 | 54 | 55 | 84 | 85 | 86 | 87 |
 * +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 * | 40 | 41 | 42 | 43 | 72 | 73 | 74 | 75 | 56 | 57 | 58 | 59 | 88 | 89 | 90 | 91 |
 * +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 * | 44 | 45 | 46 | 47 | 76 | 77 | 78 | 79 | 60 | 61 | 62 | 63 | 92 | 93 | 94 | 95 |
 * +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 *    L              F              R              B
 *                     | 16 | 17 | 18 | 19 |
 *                     +----+----+----+----+
 *                     | 20 | 21 | 22 | 23 |
 *                     +----+----+----+----+
 *                     | 24 | 25 | 26 | 27 |
 *                     +----+----+----+----+
 *                     | 28 | 29 | 30 | 31 |
 *                     +----+----+----+----+
 *
 * At the L/D fold junction, stickers 46 | 47 (bottom of L) and 16 (top of D)
 * meet at a corner — 47 and 16 are the L-face and D-face stickers of the DFL
 * corner piece; 46 is the adjacent DL wing.
 *
 * -----------------------------------------------------------------------------
 * PIECES  (see piece4.h for PieceLabel4 enum)
 * -----------------------------------------------------------------------------
 *
 * 56 pieces: 8 corners + 24 wings + 24 X-centres.
 *
 * WING NAMING CONVENTION
 *   For edge XY, _A is the wing with the lower absolute slot index on face X
 *   (the first-named face).  _B is the other wing on the same edge.
 *
 * FACELET → PIECE  (solved-state assignment for all 96 slots)
 *
 *   U face (0–15):
 *     0=PC4_UBL    1=PC4_UB_A   2=PC4_UB_B   3=PC4_UBR
 *     4=PC4_UL_A   5=PC4_U_BL   6=PC4_U_BR   7=PC4_UR_A
 *     8=PC4_UL_B   9=PC4_U_FL  10=PC4_U_FR  11=PC4_UR_B
 *    12=PC4_UFL   13=PC4_UF_A  14=PC4_UF_B  15=PC4_UFR
 *
 *   D face (16–31):
 *    16=PC4_DFL   17=PC4_DF_A  18=PC4_DF_B  19=PC4_DFR
 *    20=PC4_DL_A  21=PC4_D_FL  22=PC4_D_FR  23=PC4_DR_A
 *    24=PC4_DL_B  25=PC4_D_BL  26=PC4_D_BR  27=PC4_DR_B
 *    28=PC4_DBL   29=PC4_DB_A  30=PC4_DB_B  31=PC4_DBR
 *
 *   L face (32–47):
 *    32=PC4_UBL   33=PC4_UL_A  34=PC4_UL_B  35=PC4_UFL
 *    36=PC4_BL_A  37=PC4_L_UB  38=PC4_L_UF  39=PC4_FL_A
 *    40=PC4_BL_B  41=PC4_L_DB  42=PC4_L_DF  43=PC4_FL_B
 *    44=PC4_DBL   45=PC4_DL_B  46=PC4_DL_A  47=PC4_DFL
 *
 *   R face (48–63):
 *    48=PC4_UFR   49=PC4_UR_B  50=PC4_UR_A  51=PC4_UBR
 *    52=PC4_FR_A  53=PC4_R_UF  54=PC4_R_UB  55=PC4_BR_A
 *    56=PC4_FR_B  57=PC4_R_DF  58=PC4_R_DB  59=PC4_BR_B
 *    60=PC4_DFR   61=PC4_DR_A  62=PC4_DR_B  63=PC4_DBR
 *
 *   F face (64–79):
 *    64=PC4_UFL   65=PC4_UF_A  66=PC4_UF_B  67=PC4_UFR
 *    68=PC4_FL_A  69=PC4_F_UL  70=PC4_F_UR  71=PC4_FR_A
 *    72=PC4_FL_B  73=PC4_F_DL  74=PC4_F_DR  75=PC4_FR_B
 *    76=PC4_DFL   77=PC4_DF_A  78=PC4_DF_B  79=PC4_DFR
 *
 *   B face (80–95):
 *    80=PC4_UBR   81=PC4_UB_B  82=PC4_UB_A  83=PC4_UBL
 *    84=PC4_BR_A  85=PC4_B_UR  86=PC4_B_UL  87=PC4_BL_A
 *    88=PC4_BR_B  89=PC4_B_DR  90=PC4_B_DL  91=PC4_BL_B
 *    92=PC4_DBR   93=PC4_DB_B  94=PC4_DB_A  95=PC4_DBL
 *
 * CROSS-CHECK
 *   Each corner appears exactly 3 times, each wing exactly 2 times,
 *   each centre exactly 1 time: 8×3 + 24×2 + 24×1 = 96. ✓
 * -----------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include "piece4.h"
#include "cube3.h"   /* reuses Face enum and typedef struct Alg Alg forward-decl */

#define FACELET4_COUNT 96
#define MAX_4X4_ORDER  0  /* TODO: determine exact value */

typedef struct {
    uint8_t state[FACELET4_COUNT];
} CubeState4;

typedef uint64_t CycleSet4;

#define CYCLESET4_EMPTY ((CycleSet4)0ULL)
#define CYCLESET4_FULL  ((CycleSet4)((1ULL << PC4_COUNT) - 1ULL))

static inline bool cycleset4_disjoint(CycleSet4 a, CycleSet4 b) {
    return (a & b) == 0;
}

static inline CycleSet4 cycleset4_union(CycleSet4 a, CycleSet4 b) {
    return a | b;
}

void cube4_init(void);

void cube4_identity(CubeState4 *s);
void cube4_copy(CubeState4 *dst, const CubeState4 *src);
bool cube4_is_identity(const CubeState4 *s);
bool cube4_equal(const CubeState4 *a, const CubeState4 *b);

/* face: one of the FACE_* constants from cube3.h
 * quarter_turns: 1=CW, 2=180°, 3=CCW
 * depth: 1=outer face only, 2=wide (outer + adjacent inner slice) */
void cube4_apply_move(CubeState4 *s, Face face, int quarter_turns, int depth);
void cube4_apply_sequence(CubeState4 *s, const Alg *a);

int       cube4_compute_order(const Alg *a);
CycleSet4 cube4_cycleset_from_alg(const Alg *a);

/* IDA* search: find an algorithm that produces *target from identity.
 * Searches up to max_depth moves (outer + wide move set, 36 total).
 * On success fills *out (caller must alg_free) and returns true. */
bool cube4_idas_find(const CubeState4 *target, int max_depth, Alg *out);

extern PieceLabel4 facelet_to_piece4[FACELET4_COUNT];

void cube4_print_state(const CubeState4 *s);
void cycleset4_print(CycleSet4 cs);

#endif /* CUBE4_H */
