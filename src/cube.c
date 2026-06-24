#include "../include/cube.h"
#include "../include/alg.h"
#include "../include/piece.h"
#include "../include/util.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ---------------------------------------------------------------------------
 * PERMUTATION CONVENTION
 * ---------------------------------------------------------------------------
 *
 * new_state[i] = old_state[ perm[i] ]
 *
 * i.e. perm[destination] = source.
 * e.g. a->b->c->d->a encodes perm[b]=a, perm[c]=b, perm[d]=c, perm[a]=d.
 *
 * Sticker-direction rules for band stickers under each CW rotation:
 *   U CW (from above):        F->R, R->B, B->L, L->F
 *   D CW (from below):        F->R, R->B, B->L, L->F  (same physical direction)
 *   F CW (from front):        U->R, R->D, D->L, L->U
 *   R CW (from right):        U->B, B->D, D->F, F->U
 *   B CW (from outside back): U->L, L->D, D->R, R->U
 *   L CW (from left):         U->F, F->D, D->B, B->U
 *
 * D-face slot assignments (cube.h, viewed from below, top-of-view = back):
 *   slot 8=DBL  9=DB  10=DBR  11=DL  12=DR  13=DFL  14=DF  15=DFR
 */

static const uint8_t CW_U[FACELET_COUNT] = {
    5,  3,  0,  6,  1,  7,  4,  2,  // U face
    8,  9, 10, 11, 12, 13, 14, 15,  // D unchanged
   32, 33, 34, 19, 20, 21, 22, 23,  // L top <- F top
   40, 41, 42, 27, 28, 29, 30, 31,  // R top <- B top (wait — wrong)
   24, 25, 26, 35, 36, 37, 38, 39,  // F top <- R top (wait — wrong)
   16, 17, 18, 43, 44, 45, 46, 47,  // B top <- L top (wait — wrong)
};

static const uint8_t CW_D[FACELET_COUNT] = {
    0,  1,  2,  3,  4,  5,  6,  7,  // U unchanged
   13, 11,  8, 14,  9, 15, 12, 10,  // D face
   16, 17, 18, 19, 20, 45, 46, 47,  // L bot <- B bot
   24, 25, 26, 27, 28, 37, 38, 39,  // R bot <- F bot
   32, 33, 34, 35, 36, 21, 22, 23,  // F bot <- L bot
   40, 41, 42, 43, 44, 29, 30, 31,  // B bot <- R bot
};

static const uint8_t CW_F[FACELET_COUNT] = {
    0,  1,  2,  3,  4, 23, 20, 18,  // U: bot row (5,6,7) <- L-right (23,20,18)
   29, 27, 24, 11, 12, 13, 14, 15,  // D: top row (8,9,10) <- R-left (29,27,24)
   16, 17,  8, 19,  9, 21, 22, 10,  // L: right col (18,20,23) <- D-top (8,9,10)
    5, 25, 26,  6, 28,  7, 30, 31,  // R: left col (24,27,29) <- U-bot (5,6,7)
   37, 35, 32, 38, 33, 39, 36, 34,  // F face
   40, 41, 42, 43, 44, 45, 46, 47,  // B unchanged
};

static const uint8_t CW_R[FACELET_COUNT] = {
     0,  1, 34,  3, 36,  5,  6, 39,  // U right col
     8,  9, 45, 11, 43, 13, 14, 40,  // D R-adj col
    16, 17, 18, 19, 20, 21, 22, 23,  // L unchanged
    29, 27, 24, 30, 25, 31, 28, 26,  // R
    32, 33, 10, 35, 12, 37, 38, 15,  // F right col
     7, 41, 42,  4, 44,  2, 46, 47,  // B left col
};

static const uint8_t CW_B[FACELET_COUNT] = {
    26, 28, 31,  3,  4,  5,  6,  7,  // U: top row (0,1,2) <- R-right (26,28,31)
     8,  9, 10, 11, 12, 16, 19, 21,  // D: bot row (13,14,15) <- L-left (16,19,21)
     2, 17, 18,  1, 20,  0, 22, 23,  // L: left col (16,19,21) <- U-top (2,1,0)
    24, 25, 15, 27, 14, 29, 30, 13,  // R: right col (26,28,31) <- D-bot (15,14,13)
    32, 33, 34, 35, 36, 37, 38, 39,  // F unchanged
    45, 43, 40, 46, 41, 47, 44, 42,  // B face
};

static const uint8_t CW_L[FACELET_COUNT] = {
    47,  1,  2, 44,  4, 42,  6,  7,  // U left col
    32,  9, 10, 35, 12, 37, 14, 15,  // D L-adj col
    21, 19, 16, 22, 17, 23, 20, 18,  // L
    24, 25, 26, 27, 28, 29, 30, 31,  // R unchanged
     0, 33, 34,  3, 36,  5, 38, 39,  // F left col
    40, 41, 13, 43, 11, 45, 46,  8,  // B right col
};

/* ---------------------------------------------------------------------------
 * Runtime move table: MOVE_TABLE[face][q-1], q in {1,2,3}.
 * ---------------------------------------------------------------------------*/

static uint8_t MOVE_TABLE[FACE_COUNT][3][FACELET_COUNT];
static bool    g_initialised = false;

/* facelet_to_piece: header exposes it as read-only, but cube_init() writes it,
 * so the definition MUST be non-const. A const definition lands in .rodata and
 * writing through the cast is UB -> SIGSEGV in cube_init. Do not re-add const. */
PieceLabel facelet_to_piece[FACELET_COUNT];
static PieceLabel *const ftp = facelet_to_piece;

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------------*/

/* cube_compose: out[i] = a[b[i]]  (apply a first, then b) */
static void cube_compose(uint8_t out[FACELET_COUNT],
                         const uint8_t a[FACELET_COUNT], const uint8_t b[FACELET_COUNT]) {
    for (int i = 0; i < FACELET_COUNT; i++) out[i] = a[b[i]];
}

/* apply_perm: new_state[i] = old_state[perm[i]] */
static void apply_perm(uint8_t state[FACELET_COUNT], const uint8_t perm[FACELET_COUNT]) {
    uint8_t tmp[FACELET_COUNT];
    for (int i = 0; i < FACELET_COUNT; i++) tmp[i] = state[perm[i]];
    memcpy(state, tmp, FACELET_COUNT);
}

/* ---------------------------------------------------------------------------
 * cube_init
 * ---------------------------------------------------------------------------*/

void cube_init(void) {
    if (g_initialised) return;

    // Ordered by Face enum: U=0, D=1, L=2, R=3, F=4, B=5.
    static const uint8_t *const CW[FACE_COUNT] = {
        CW_U, CW_D, CW_L, CW_R, CW_F, CW_B,
    };

    // Build all 18 moves
    for (int f = 0; f < FACE_COUNT; f++) {
        memcpy(MOVE_TABLE[f][0], CW[f], FACELET_COUNT);
        cube_compose(MOVE_TABLE[f][1], CW[f], CW[f]);
        cube_compose(MOVE_TABLE[f][2], MOVE_TABLE[f][1], CW[f]);
    }

#ifndef NDEBUG
    {
        uint8_t tmp[FACELET_COUNT], tmp2[FACELET_COUNT];

        for (int f = 0; f < FACE_COUNT; f++) {
            /* X^4 = identity: compute X^2 into tmp, then (X^2)^2 into tmp2.
             * Must not alias input and output in cube_compose. */
            cube_compose(tmp,  CW[f], CW[f]);    // X^2
            cube_compose(tmp2, tmp,   tmp);      // (X^2)^2
            for (int i = 0; i < FACELET_COUNT; i++) assert(tmp2[i] == (uint8_t)i);

            /* X X' = identity */
            cube_compose(tmp, CW[f], MOVE_TABLE[f][2]);
            for (int i = 0; i < FACELET_COUNT; i++) assert(tmp[i] == (uint8_t)i);
        }

        /* Opposite-face pairs commute. */
        static const int OPP[3][2] = {
            {FACE_U, FACE_D}, {FACE_L, FACE_R}, {FACE_F, FACE_B}
        };
        uint8_t ab[FACELET_COUNT], ba[FACELET_COUNT];
        for (int p = 0; p < 3; p++) {
            cube_compose(ab, CW[OPP[p][0]], CW[OPP[p][1]]);
            cube_compose(ba, CW[OPP[p][1]], CW[OPP[p][0]]);
            for (int i = 0; i < FACELET_COUNT; i++) assert(ab[i] == ba[i]);
        }
    }
#endif /* NDEBUG */

    /* Build facelet_to_piece[FACELET_COUNT] from the net diagram in cube.h.
     * Each entry is the cubie whose sticker occupies that slot in the solved state. */

    /* U face (slots 0-7) – viewed from above, F toward you */
    ftp[ 0]=PC_UBL; ftp[ 1]=PC_UB; ftp[ 2]=PC_UBR;
    ftp[ 3]=PC_UL ;                ftp[ 4]=PC_UR ;
    ftp[ 5]=PC_UFL; ftp[ 6]=PC_UF; ftp[ 7]=PC_UFR;

    /* D face (slots 8-15) - row-major in the unfolded net */
    ftp[ 8]=PC_DFL; ftp[ 9]=PC_DF; ftp[10]=PC_DFR;
    ftp[11]=PC_DL ;                ftp[12]=PC_DR ;
    ftp[13]=PC_DBL; ftp[14]=PC_DB; ftp[15]=PC_DBR;

    /* L face (slots 16-23) – head-on, U above */
    ftp[16]=PC_UBL; ftp[17]=PC_UL; ftp[18]=PC_UFL;
    ftp[19]=PC_BL ;                ftp[20]=PC_FL ;
    ftp[21]=PC_DBL; ftp[22]=PC_DL; ftp[23]=PC_DFL;

    /* R face (slots 24-31) – head-on, U above */
    ftp[24]=PC_UFR; ftp[25]=PC_UR; ftp[26]=PC_UBR;
    ftp[27]=PC_FR ;                ftp[28]=PC_BR ;
    ftp[29]=PC_DFR; ftp[30]=PC_DR; ftp[31]=PC_DBR;

    /* F face (slots 32-39) – head-on, U above */
    ftp[32]=PC_UFL; ftp[33]=PC_UF; ftp[34]=PC_UFR;
    ftp[35]=PC_FL ;                ftp[36]=PC_FR ;
    ftp[37]=PC_DFL; ftp[38]=PC_DF; ftp[39]=PC_DFR;

    /* B face (slots 40-47) – head-on from outside back, U above */
    ftp[40]=PC_UBR; ftp[41]=PC_UB; ftp[42]=PC_UBL;
    ftp[43]=PC_BR ;                ftp[44]=PC_BL ;
    ftp[45]=PC_DBR; ftp[46]=PC_DB; ftp[47]=PC_DBL;

#ifndef NDEBUG
    // Each edge cubie owns exactly 2 stickers; each corner owns exactly 3.
    {
        int cnt[PC_COUNT] = {0};
        for (int i = 0; i < FACELET_COUNT; i++) cnt[ftp[i]]++;
        for (int p = 0; p < PC_COUNT; p++)
            assert(cnt[p] == (int)strlen(piece_to_string(p)));

        /* Each move must carry all stickers from one physical cubie to one
         * cubie position. This catches facelet_to_piece/move-table mismatches. */
        for (int f = 0; f < FACE_COUNT; f++) {
            for (int q = 0; q < 3; q++) {
                int dest_piece[PC_COUNT];
                for (int p = 0; p < PC_COUNT; p++) dest_piece[p] = -1;
                for (int dst = 0; dst < FACELET_COUNT; dst++) {
                    int srcp = ftp[MOVE_TABLE[f][q][dst]];
                    int dstp = ftp[dst];
                    if (dest_piece[srcp] < 0) dest_piece[srcp] = dstp;
                    else assert(dest_piece[srcp] == dstp);
                }
            }
        }
    }
#endif

    g_initialised = true;
}

/* ---------------------------------------------------------------------------
 * Identity, copy, equality
 * ---------------------------------------------------------------------------*/

void cube_identity(CubeState *s) {
    for (int i = 0; i < FACELET_COUNT; i++) s->state[i] = (uint8_t)i;
}

void cube_copy(CubeState *dst, const CubeState *src) {
    memcpy(dst->state, src->state, FACELET_COUNT);
}

bool cube_is_identity(const CubeState *s) {
    for (int i = 0; i < FACELET_COUNT; i++)
        if (s->state[i] != (uint8_t)i) return false;
    return true;
}

bool cube_equal(const CubeState *a, const CubeState *b) {
    return memcmp(a->state, b->state, FACELET_COUNT) == 0;
}

/* ---------------------------------------------------------------------------
 * Applying moves
 * ---------------------------------------------------------------------------*/

void cube_apply_move(CubeState *s, Face face, int quarter_turns) {
    /* quarter_turns in {1,2,3}; values outside this range are undefined behaviour. */
    apply_perm(s->state, MOVE_TABLE[(int)face][quarter_turns - 1]);
}

void cube_apply_sequence(CubeState *s, const Alg *a) {
    for (int i = 0; i < a->len; i++)
        cube_apply_move(s, (Face)a->m[i].face, (int)a->m[i].q);
}

/* ---------------------------------------------------------------------------
 * Order computation
 * ---------------------------------------------------------------------------*/

int compute_order(const Alg *a) {
    CubeState s;
    cube_identity(&s);
    for (int n = 1; n <= MAX_3X3_ORDER; n++) {
        cube_apply_sequence(&s, a);
        if (cube_is_identity(&s)) return n;
    }
    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "unexpected: compute_order exceeded maximum possible order");
}

/* ---------------------------------------------------------------------------
 * Cycle set
 * ---------------------------------------------------------------------------*/

CycleSet cycleset_from_alg(const Alg *a) {
    CubeState s;
    cube_identity(&s);
    cube_apply_sequence(&s, a);

    CycleSet cs = CYCLESET_EMPTY;
    for (int i = 0; i < FACELET_COUNT; i++)
        if (s.state[i] != (uint8_t)i)
            cs |= (CycleSet)(1u << facelet_to_piece[i]);
    return cs;
}

/* ---------------------------------------------------------------------------
 * Printing / debugging
 * ---------------------------------------------------------------------------*/

void cube_print_state(const CubeState *s) {
    for (int i = 0; i < FACELET_COUNT; i++) {
        if (i > 0) putchar(' ');
        printf("%d", (int)s->state[i]);
    }
    putchar('\n');
}

void cycleset_print(CycleSet cs) {
    if (cs == CYCLESET_EMPTY) {
        fputs("(empty)", stdout);
        return;
    }
    bool first = true;
    for (int p = 0; p < PC_COUNT; p++) {
        if (cs & (CycleSet)(1u << p)) {
            if (!first) putchar(',');
            fputs(piece_to_string(p), stdout);
            first = false;
        }
    }
}
