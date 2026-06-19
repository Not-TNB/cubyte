/*
 * Vendored API (confirmed from third_party/ckociemba/include/search.h and solve.c):
 *
 *   char* solution(char* facelets, int maxDepth, long timeOut,
 *                  int useSeparator, const char* cache_dir);
 *
 * Returns a heap-allocated, space-separated move string (caller must free()),
 * or NULL on all failure paths. Error strings ("Error N") documented in the
 * header are not returned by the C implementation — all errors yield NULL.
 * Pruning tables are loaded lazily from cache_dir on first call.
 *
 * We invoke ckociemba as a subprocess rather than linking it, to avoid GPLv2
 * contamination of cubyte's own source. The binary reads CKOCIEMBA_CACHE from
 * the environment for the pruning table path (patched in solve.c).
 *
 * cubyte must be run from the repo root, or CKOCIEMBA_BIN / CKOCIEMBA_CACHE
 * must be overridden at compile time with absolute paths.
 */

/* popen/pclose are POSIX, not ISO C; request them under -std=c17. */
#define _POSIX_C_SOURCE 200809L

#include "../include/kociemba.h"
#include "../include/cube.h"
#include "../include/alg.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#ifndef CKOCIEMBA_BIN
#define CKOCIEMBA_BIN "../third_party/ckociemba/bin/kociemba"
#endif
#ifndef CKOCIEMBA_CACHE
#define CKOCIEMBA_CACHE "../third_party/ckociemba/cprunetables"
#endif

/*
 * Maps our face index (slot / 8) to the URFDLB character ckociemba expects.
 * Our face enum: U=0, D=1, L=2, R=3, F=4, B=5
 */
static const char kFaceChar[6] = {'U', 'D', 'L', 'R', 'F', 'B'};

static char color_at(const CubeState *s, int slot) {
    return kFaceChar[s->state[slot] / 8];
}

/*
 * Translate our 48-slot CubeState into ckociemba's 54-char URFDLB facelet string.
 * Centres never move so they are hardcoded per face.
 *
 * ckociemba string layout (from facelet.h): U1-U9, R1-R9, F1-F9, D1-D9, L1-L9, B1-B9.
 * Each face is row-major from top-left in the standard unfolded net:
 *
 *              U1 U2 U3
 *              U4 U5 U6
 *              U7 U8 U9
 *  L1 L2 L3 | F1 F2 F3 | R1 R2 R3 | B1 B2 B3
 *  L4 L5 L6 | F4 F5 F6 | R4 R5 R6 | B4 B5 B6
 *  L7 L8 L9 | F7 F8 F9 | R7 R8 R9 | B7 B8 B9
 *              D1 D2 D3
 *              D4 D5 D6
 *              D7 D8 D9
 *
 * Our net (cube.h):
 *
 *               0  1  2
 *               3  U  4
 *               5  6  7
 *  16 17 18 | 32 33 34 | 24 25 26 | 40 41 42
 *  19  L 20 | 35  F 36 | 27  R 28 | 43  B 44
 *  21 22 23 | 37 38 39 | 29 30 31 | 45 46 47
 *               8  9 10   (D1-D3 = DFL/DF/DFR)
 *              11  D 12
 *              13 14 15
 */
static void cube_to_kociemba_facelets(const CubeState *s, char out[55]) {
    /* U face */
    out[0]  = color_at(s, 0);   /* U1 = UBL */
    out[1]  = color_at(s, 1);   /* U2 = UB  */
    out[2]  = color_at(s, 2);   /* U3 = UBR */
    out[3]  = color_at(s, 3);   /* U4 = UL  */
    out[4]  = 'U';              /* U5 = centre */
    out[5]  = color_at(s, 4);   /* U6 = UR  */
    out[6]  = color_at(s, 5);   /* U7 = UFL */
    out[7]  = color_at(s, 6);   /* U8 = UF  */
    out[8]  = color_at(s, 7);   /* U9 = UFR */

    /* R face */
    out[9]  = color_at(s, 24);  /* R1 = UFR */
    out[10] = color_at(s, 25);  /* R2 = UR  */
    out[11] = color_at(s, 26);  /* R3 = UBR */
    out[12] = color_at(s, 27);  /* R4 = FR  */
    out[13] = 'R';              /* R5 = centre */
    out[14] = color_at(s, 28);  /* R6 = BR  */
    out[15] = color_at(s, 29);  /* R7 = DFR */
    out[16] = color_at(s, 30);  /* R8 = DR  */
    out[17] = color_at(s, 31);  /* R9 = DBR */

    /* F face */
    out[18] = color_at(s, 32);  /* F1 = UFL */
    out[19] = color_at(s, 33);  /* F2 = UF  */
    out[20] = color_at(s, 34);  /* F3 = UFR */
    out[21] = color_at(s, 35);  /* F4 = FL  */
    out[22] = 'F';              /* F5 = centre */
    out[23] = color_at(s, 36);  /* F6 = FR  */
    out[24] = color_at(s, 37);  /* F7 = DFL */
    out[25] = color_at(s, 38);  /* F8 = DF  */
    out[26] = color_at(s, 39);  /* F9 = DFR */

    /* D face */
    out[27] = color_at(s, 8);  /* D1 = DFL */
    out[28] = color_at(s, 9);  /* D2 = DF  */
    out[29] = color_at(s, 10);  /* D3 = DFR */
    out[30] = color_at(s, 11);  /* D4 = DL  */
    out[31] = 'D';              /* D5 = centre */
    out[32] = color_at(s, 12);  /* D6 = DR  */
    out[33] = color_at(s, 13);   /* D7 = DBL */
    out[34] = color_at(s, 14);   /* D8 = DB  */
    out[35] = color_at(s, 15);  /* D9 = DBR */

    /* L face */
    out[36] = color_at(s, 16);  /* L1 = UBL */
    out[37] = color_at(s, 17);  /* L2 = UL  */
    out[38] = color_at(s, 18);  /* L3 = UFL */
    out[39] = color_at(s, 19);  /* L4 = BL  */
    out[40] = 'L';              /* L5 = centre */
    out[41] = color_at(s, 20);  /* L6 = FL  */
    out[42] = color_at(s, 21);  /* L7 = DBL */
    out[43] = color_at(s, 22);  /* L8 = DL  */
    out[44] = color_at(s, 23);  /* L9 = DFL */

    /* B face: unfolded rightward of R in the net, so left-right is mirrored.
     * B1(top-left in net) = UBR, B3(top-right) = UBL. Slots 40-47. */
    out[45] = color_at(s, 40);  /* B1 = UBR */
    out[46] = color_at(s, 41);  /* B2 = UB  */
    out[47] = color_at(s, 42);  /* B3 = UBL */
    out[48] = color_at(s, 43);  /* B4 = BR  */
    out[49] = 'B';              /* B5 = centre */
    out[50] = color_at(s, 44);  /* B6 = BL  */
    out[51] = color_at(s, 45);  /* B7 = DBR */
    out[52] = color_at(s, 46);  /* B8 = DB  */
    out[53] = color_at(s, 47);  /* B9 = DBL */

    out[54] = '\0';
}

bool kociemba_solve_state(const CubeState *target, Alg *out) {
    static bool initialised = false;
    if (!initialised) { cube_init(); initialised = true; }

    /* ckociemba may error on an already-solved cube; the empty alg produces it. */
    if (cube_is_identity(target)) {
        *out = (Alg){0};
        return true;
    }

    char facelets[55];
    cube_to_kociemba_facelets(target, facelets);
    if (getenv("KDBG")) fprintf(stderr, "[kdbg] facelets=%s\n", facelets);

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "CKOCIEMBA_CACHE='" CKOCIEMBA_CACHE "' '" CKOCIEMBA_BIN "' %s",
             facelets);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return false;

    char buf[256] = {0};
    bool got_output = (fgets(buf, sizeof(buf), pipe) != NULL);
    int exit_status = pclose(pipe);

    if (!got_output || exit_status != 0) return false;

    buf[strcspn(buf, "\n")] = '\0';

    /* ckociemba prints "Unsolvable cube!" on bad input (exit 2) */
    if (buf[0] == '\0' || strncmp(buf, "Unsolvable", 10) == 0) return false;

    /* ckociemba returns the sequence that SOLVES the state (state -> identity).
     * Inverting it yields the sequence identity -> state, i.e. one that produces
     * *target from the solved cube — exactly the register's "add 1" algorithm. */
    Alg sol = {0};
    if (!alg_parse(buf, &sol)) return false;

    alg_invert(&sol, out);
    alg_free(&sol);
    alg_simplify(out);

    return true;
}

char *kociemba_shorten(const char *raw_alg) {
    Alg a = {0};
    if (!alg_parse(raw_alg, &a)) return NULL;

    cube_init();

    CubeState s;
    cube_identity(&s);
    cube_apply_sequence(&s, &a);
    alg_free(&a);

    Alg out = {0};
    if (!kociemba_solve_state(&s, &out)) return NULL;

    char *result = alg_to_string(&out);
    alg_free(&out);
    return result;
}
