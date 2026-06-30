#include "../../include/cube/cube4.h"
#include "../../include/cube/piece4.h"
#include "../../include/cube/alg.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Solved-state slot → piece mapping (see cube4.h for the full derivation). */
PieceLabel4 facelet_to_piece4[FACELET4_COUNT] = {
    /* U (0-15) */
    PC4_UBL,  PC4_UB_A, PC4_UB_B, PC4_UBR,
    PC4_UL_A, PC4_U_BL, PC4_U_BR, PC4_UR_A,
    PC4_UL_B, PC4_U_FL, PC4_U_FR, PC4_UR_B,
    PC4_UFL,  PC4_UF_A, PC4_UF_B, PC4_UFR,
    /* D (16-31) */
    PC4_DFL,  PC4_DF_A, PC4_DF_B, PC4_DFR,
    PC4_DL_A, PC4_D_FL, PC4_D_FR, PC4_DR_A,
    PC4_DL_B, PC4_D_BL, PC4_D_BR, PC4_DR_B,
    PC4_DBL,  PC4_DB_A, PC4_DB_B, PC4_DBR,
    /* L (32-47) */
    PC4_UBL,  PC4_UL_A, PC4_UL_B, PC4_UFL,
    PC4_BL_A, PC4_L_UB, PC4_L_UF, PC4_FL_A,
    PC4_BL_B, PC4_L_DB, PC4_L_DF, PC4_FL_B,
    PC4_DBL,  PC4_DL_B, PC4_DL_A, PC4_DFL,
    /* R (48-63) */
    PC4_UFR,  PC4_UR_B, PC4_UR_A, PC4_UBR,
    PC4_FR_A, PC4_R_UF, PC4_R_UB, PC4_BR_A,
    PC4_FR_B, PC4_R_DF, PC4_R_DB, PC4_BR_B,
    PC4_DFR,  PC4_DR_A, PC4_DR_B, PC4_DBR,
    /* F (64-79) */
    PC4_UFL,  PC4_UF_A, PC4_UF_B, PC4_UFR,
    PC4_FL_A, PC4_F_UL, PC4_F_UR, PC4_FR_A,
    PC4_FL_B, PC4_F_DL, PC4_F_DR, PC4_FR_B,
    PC4_DFL,  PC4_DF_A, PC4_DF_B, PC4_DFR,
    /* B (80-95) */
    PC4_UBR,  PC4_UB_B, PC4_UB_A, PC4_UBL,
    PC4_BR_A, PC4_B_UR, PC4_B_UL, PC4_BL_A,
    PC4_BR_B, PC4_B_DR, PC4_B_DL, PC4_BL_B,
    PC4_DBR,  PC4_DB_B, PC4_DB_A, PC4_DBL,
};

/* ---------------------------------------------------------------------------
 * Move tables
 *
 * move_cw[face][slot] = destination slot after one CW quarter-turn on that
 * face at depth 1 (outer only).  move_wide_cw[face][slot] is the same for
 * depth 2 (outer + adjacent inner slice, i.e. Uw, Lw, Rw, Fw, Bw, Dw).
 *
 * Slots not in the moved layer are left at their own index (identity).
 * All 36 moves are built by composing these two tables and their powers.
 * ---------------------------------------------------------------------------
 */

/* Each entry is a permutation on [0,96). */
typedef uint8_t Perm96[FACELET4_COUNT];

static Perm96 move_cw[6];        /* outer CW quarter-turn per face */
static Perm96 move_wide_cw[6];   /* wide  CW quarter-turn per face */

/* Precomputed full move table: perm[face][depth-1][qt-1] where qt in {1,2,3} */
static Perm96 move_table[6][2][3];

/* Build identity permutation. */
static void perm_identity(uint8_t *p) {
    for (int i = 0; i < FACELET4_COUNT; i++) p[i] = (uint8_t)i;
}

/* Compose: result[i] = a[b[i]]  (apply b first, then a). */
static void perm_compose(const uint8_t *a, const uint8_t *b, uint8_t *out) {
    for (int i = 0; i < FACELET4_COUNT; i++) out[i] = a[b[i]];
}

/* Power: out = p^n (repeated composition). */
static void perm_power(const uint8_t *p, int n, uint8_t *out) {
    uint8_t tmp[FACELET4_COUNT];
    perm_identity(out);
    for (int i = 0; i < n; i++) {
        perm_compose(p, out, tmp);
        memcpy(out, tmp, FACELET4_COUNT);
    }
}

/* Apply permutation p to state s in-place. */
static void perm_apply(const uint8_t *p, uint8_t *s) {
    uint8_t tmp[FACELET4_COUNT];
    for (int i = 0; i < FACELET4_COUNT; i++) tmp[i] = s[p[i]];
    memcpy(s, tmp, FACELET4_COUNT);
}

/* ---------------------------------------------------------------------------
 * Face-cycle helpers
 *
 * A CW quarter-turn of a face cycles its 16 facelets (4 corners + 8 wings +
 * 4 centres) and cycles the 12 adjacent facelets on the 4 neighbouring faces
 * (3 columns/rows per neighbour × 4 = 12). We encode these as a list of
 * 4-element cycles.
 * ---------------------------------------------------------------------------
 */

static void set_4cycle(uint8_t *p, int a, int b, int c, int d) {
    /* Piece at a moves to b, b→c, c→d, d→a.
     * perm_apply uses tmp[i]=s[p[i]], so "piece at src moves to dst" = p[dst]=src. */
    p[b] = (uint8_t)a;
    p[c] = (uint8_t)b;
    p[d] = (uint8_t)c;
    p[a] = (uint8_t)d;
}

/* Build the 16 intra-face cycles for a CW quarter-turn. */
static void face_self_cycles(uint8_t *p, int base) {
    /* corners */
    set_4cycle(p, base+0,  base+3,  base+15, base+12);
    /* wings */
    set_4cycle(p, base+1,  base+7,  base+14, base+8);
    set_4cycle(p, base+2,  base+11, base+13, base+4);
    /* centres */
    set_4cycle(p, base+5,  base+6,  base+10, base+9);
}

// static void build_U_outer(uint8_t *p) {
//     perm_identity(p);
//     face_self_cycles(p, 0); /* U base = 0 */
//     for (int c = 0; c < 4; c++)
//         set_4cycle(p, 64+c, 48+c, 80+c, 32+c);
// }
// static void build_U_wide(uint8_t *p) {
//     build_U_outer(p); /* includes face self-cycles */
//     for (int c = 0; c < 4; c++)
//         set_4cycle(p, 68+c, 52+c, 84+c, 36+c);
// }
//
//
// static void build_D_outer(uint8_t *p) {
//     perm_identity(p);
//     face_self_cycles(p, 16);
//     for (int c = 0; c < 4; c++)
//         set_4cycle(p, 76+c, 44+c, 95-c, 63-c);
// }
// static void build_D_wide(uint8_t *p) {
//     build_D_outer(p);
//     /* inner slice = row 2 of side faces */
//     /* F row2: 72,73,74,75  L row2: 40,41,42,43
//        B row2: 88,89,90,91  R row2: 56,57,58,59 */
//     for (int c = 0; c < 4; c++)
//         set_4cycle(p, 72+c, 40+c, 91-c, 59-c);
// }

static void build_U_outer(uint8_t *p) {
    perm_identity(p);
    face_self_cycles(p, 0); /* U base = 0 */
    for (int c = 0; c < 4; c++)
        set_4cycle(p, 64+c, 32+c, 80+c, 48+c);
}
static void build_U_wide(uint8_t *p) {
    build_U_outer(p); /* includes face self-cycles */
    for (int c = 0; c < 4; c++)
        set_4cycle(p, 68+c, 36+c, 84+c, 52+c);
}


static void build_D_outer(uint8_t *p) {
    perm_identity(p);
    face_self_cycles(p, 16);
    for (int c = 0; c < 4; c++)
        set_4cycle(p, 76+c, 60+c, 92+c, 44+c);
}
static void build_D_wide(uint8_t *p) {
    build_D_outer(p);
    for (int c = 0; c < 4; c++)
        set_4cycle(p, 72+c, 56+c, 88+c, 40+c);
}


static void build_R_outer(uint8_t *p) {
    perm_identity(p);
    face_self_cycles(p, 48);
    set_4cycle(p, 67, 3,  92, 19);
    set_4cycle(p, 71, 7,  88, 23);
    set_4cycle(p, 75, 11, 84, 27);
    set_4cycle(p, 79, 15, 80, 31);
}
static void build_R_wide(uint8_t *p) {
    build_R_outer(p);
    /* inner slice = col 2 of adjacent faces */
    /* F col 2: 66,70,74,78  U col 2: 2,6,10,14
       B col 1: 81,85,89,93  D col 2: 18,22,26,30 */
    set_4cycle(p, 66, 2,  93, 18);
    set_4cycle(p, 70, 6,  89, 22);
    set_4cycle(p, 74, 10, 85, 26);
    set_4cycle(p, 78, 14, 81, 30);
}


static void build_L_outer(uint8_t *p) {
    perm_identity(p);
    face_self_cycles(p, 32);
    set_4cycle(p, 64, 16, 95, 0);
    set_4cycle(p, 68, 20, 91, 4);
    set_4cycle(p, 72, 24, 87, 8);
    set_4cycle(p, 76, 28, 83, 12);
}
static void build_L_wide(uint8_t *p) {
    build_L_outer(p);
    /* inner slice = col 1 of adjacent faces */
    /* F col 1: 65,69,73,77  U col 1: 1,5,9,13
       B col 2: 82,86,90,94  D col 1: 17,21,25,29 */
    set_4cycle(p, 65, 17, 94, 1);
    set_4cycle(p, 69, 21, 90, 5);
    set_4cycle(p, 73, 25, 86, 9);
    set_4cycle(p, 77, 29, 82, 13);
}


static void build_F_outer(uint8_t *p) {
    perm_identity(p);
    face_self_cycles(p, 64);
    set_4cycle(p, 12, 48, 19, 47);
    set_4cycle(p, 13, 52, 18, 43);
    set_4cycle(p, 14, 56, 17, 39);
    set_4cycle(p, 15, 60, 16, 35);
}
static void build_F_wide(uint8_t *p) {
    build_F_outer(p);
    /* inner slice = row 2 of U, col 1 of R, row 1 of D, col 2 of L */
    /* U row2: U8,U9,U10,U11  R col1: R49,R53,R57,R61
       D row1: D20,D21,D22,D23  L col2: L34,L38,L42,L46 */
    set_4cycle(p, 8,  49, 23, 46);
    set_4cycle(p, 9,  53, 22, 42);
    set_4cycle(p, 10, 57, 21, 38);
    set_4cycle(p, 11, 61, 20, 34);
}


static void build_B_outer(uint8_t *p) {
    perm_identity(p);
    face_self_cycles(p, 80);
    set_4cycle(p, 3,  32, 28, 63);
    set_4cycle(p, 2,  36, 29, 59);
    set_4cycle(p, 1,  40, 30, 55);
    set_4cycle(p, 0,  44, 31, 51);
}
static void build_B_wide(uint8_t *p) {
    build_B_outer(p);
    /* inner slice: U row1, L col1, D row2, R col2 */
    /* U row1: 4,5,6,7   L col1: 33,37,41,45
       D row2: 24,25,26,27  R col2: 50,54,58,62 */
    set_4cycle(p, 7,  33, 24, 62);
    set_4cycle(p, 6,  37, 25, 58);
    set_4cycle(p, 5,  41, 26, 54);
    set_4cycle(p, 4,  45, 27, 50);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------
 */

void cube4_init(void) {
    build_U_outer(move_cw[FACE_U]); build_U_wide(move_wide_cw[FACE_U]);
    build_D_outer(move_cw[FACE_D]); build_D_wide(move_wide_cw[FACE_D]);
    build_L_outer(move_cw[FACE_L]); build_L_wide(move_wide_cw[FACE_L]);
    build_R_outer(move_cw[FACE_R]); build_R_wide(move_wide_cw[FACE_R]);
    build_F_outer(move_cw[FACE_F]); build_F_wide(move_wide_cw[FACE_F]);
    build_B_outer(move_cw[FACE_B]); build_B_wide(move_wide_cw[FACE_B]);

    for (int f = 0; f < 6; f++) {
        for (int d = 0; d < 2; d++) {
            Perm96 *base = (d == 0) ? &move_cw[f] : &move_wide_cw[f];
            perm_power(*base, 1, move_table[f][d][0]);
            perm_power(*base, 2, move_table[f][d][1]);
            perm_power(*base, 3, move_table[f][d][2]);
        }
    }
}

void cube4_identity(CubeState4 *s) {
    for (int i = 0; i < FACELET4_COUNT; i++)
        s->state[i] = (uint8_t)i;
}

void cube4_copy(CubeState4 *dst, const CubeState4 *src) {
    memcpy(dst->state, src->state, FACELET4_COUNT);
}

bool cube4_is_identity(const CubeState4 *s) {
    for (int i = 0; i < FACELET4_COUNT; i++)
        if (s->state[i] != (uint8_t)i) return false;
    return true;
}

bool cube4_equal(const CubeState4 *a, const CubeState4 *b) {
    return memcmp(a->state, b->state, FACELET4_COUNT) == 0;
}

void cube4_apply_move(CubeState4 *s, Face face, int quarter_turns, int depth) {
    assert(depth == 1 || depth == 2);
    assert(quarter_turns >= 1 && quarter_turns <= 3);
    perm_apply(move_table[face][depth - 1][quarter_turns - 1], s->state);
}

void cube4_apply_sequence(CubeState4 *s, const Alg *a) {
    for (int i = 0; i < a->len; i++) {
        Move mv = a->m[i];
        cube4_apply_move(s, (Face)mv.face, mv.q, mv.depth);
    }
}

int cube4_compute_order(const Alg *a) {
    CubeState4 s, t;
    cube4_identity(&s);
    cube4_apply_sequence(&s, a);
    cube4_copy(&t, &s);
    for (int ord = 1; ord <= 1260; ord++) {
        if (cube4_is_identity(&t)) return ord;
        cube4_apply_sequence(&t, a);
    }
    return -1; /* should not happen for valid cube algorithms */
}

CycleSet4 cube4_cycleset_from_alg(const Alg *a) {
    CubeState4 s;
    cube4_identity(&s);
    cube4_apply_sequence(&s, a);

    uint64_t visited = 0;
    CycleSet4 cs = 0;

    for (int i = 0; i < FACELET4_COUNT; i++) {
        if (s.state[i] == (uint8_t)i) continue;
        /* trace cycle starting at slot i, collect piece labels */
        int cur = i;
        while (!(visited & (1ULL << facelet_to_piece4[cur]))) {
            visited |= 1ULL << facelet_to_piece4[cur];
            cs      |= 1ULL << facelet_to_piece4[cur];
            cur = s.state[cur];
        }
    }
    return cs;
}

void cube4_print_state(const CubeState4 *s) {
    for (int i = 0; i < FACELET4_COUNT; i++) {
        printf("%2d ", s->state[i]);
        if ((i & 15) == 15) printf("\n");
    }
}

void cycleset4_print(CycleSet4 cs) {
    printf("{");
    bool first = true;
    for (int p = 0; p < PC4_COUNT; p++) {
        if (cs & (1ULL << p)) {
            if (!first) printf(", ");
            printf("%s", piece4_to_string(p));
            first = false;
        }
    }
    printf("}");
}
