#include "../include/alg.h"
#include "../include/cube.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ANSI background + foreground per face (U D L R F B) */
static const char *ANSI_BG[] = {
    "\033[47;30m", /* U white  */
    "\033[43;30m", /* D yellow */
    "\033[45;37m", /* L orange (magenta approx) */
    "\033[41;37m", /* R red    */
    "\033[42;30m", /* F green  */
    "\033[44;37m", /* B blue   */
};
static const char FACE_CH[]  = { 'W', 'Y', 'O', 'R', 'G', 'B' };
static const char FACE_LBL[] = { 'U', 'D', 'L', 'R', 'F', 'B' };
#define RST "\033[0m"

/* Color of the sticker currently occupying slot. */
static int sticker_color(const CubeState *s, int slot) {
    return (int)s->state[slot] / 8;
}

static void print_sticker(const CubeState *s, int slot) {
    int f = sticker_color(s, slot);
    printf("%s %c " RST, ANSI_BG[f], FACE_CH[f]);
}

static void print_center(int face) {
    printf("%s %c " RST, ANSI_BG[face], FACE_LBL[face]);
}

/*
 * Each face has 8 non-center facelets laid out row-major:
 *   0 1 2
 *   3 . 4   (. = centre, not indexed — use -1 as sentinel)
 *   5 6 7
 */
static const int U_SLOTS[3][3] = {{ 0, 1, 2}, { 3,-1, 4}, { 5, 6, 7}};
static const int D_SLOTS[3][3] = {{ 8, 9,10}, {11,-1,12}, {13,14,15}};
static const int L_SLOTS[3][3] = {{16,17,18}, {19,-1,20}, {21,22,23}};
static const int F_SLOTS[3][3] = {{32,33,34}, {35,-1,36}, {37,38,39}};
static const int R_SLOTS[3][3] = {{24,25,26}, {27,-1,28}, {29,30,31}};
static const int B_SLOTS[3][3] = {{40,41,42}, {43,-1,44}, {45,46,47}};

static void print_face_row(const CubeState *s, const int row[3], int face_idx) {
    for (int c = 0; c < 3; c++) {
        if (row[c] < 0) print_center(face_idx);
        else            print_sticker(s, row[c]);
    }
}

/*
 * Unfolded net (each cell = 3 visible chars):
 *
 *            U U U
 *            U U U
 *            U U U
 *   L L L   F F F   R R R   B B B
 *   L L L   F F F   R R R   B B B
 *   L L L   F F F   R R R   B B B
 *            D D D
 *            D D D
 *            D D D
 *
 * U/D are offset 9 chars (= 3 cells = 1 face) to sit above/below F.
 */
static void print_cube(const CubeState *s) {
    for (int r = 0; r < 3; r++) {
        printf("         "); /* 9-char indent aligns U over F */
        print_face_row(s, U_SLOTS[r], 0);
        putchar('\n');
    }
    for (int r = 0; r < 3; r++) {
        print_face_row(s, L_SLOTS[r], 2);
        print_face_row(s, F_SLOTS[r], 4);
        print_face_row(s, R_SLOTS[r], 3);
        print_face_row(s, B_SLOTS[r], 5);
        putchar('\n');
    }
    for (int r = 0; r < 3; r++) {
        printf("         ");
        print_face_row(s, D_SLOTS[r], 1);
        putchar('\n');
    }
}

int main(void) {
    cube_init();

    CubeState cube;
    cube_identity(&cube);

    printf("Cube Explorer  —  notation: R U F L D B, ' = inverse, 2 = half-turn\n");
    printf("Commands: 'reset'/'r' to restore solved, 'q' to quit\n\n");
    print_cube(&cube);

    char line[1024];
    while (1) {
        printf("\n> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

        if ((p[0] == 'q' && p[1] == '\0') ||
            strcmp(p, "quit") == 0 || strcmp(p, "exit") == 0)
            break;

        if (strcmp(p, "reset") == 0 || strcmp(p, "r") == 0) {
            cube_identity(&cube);
            printf("Cube reset to solved.\n");
            print_cube(&cube);
            continue;
        }

        Alg alg = {0};
        if (!alg_parse(p, &alg)) {
            fprintf(stderr, "parse error: \"%s\"\n", p);
            alg_free(&alg);
            continue;
        }

        cube_apply_sequence(&cube, &alg);

        char *str = alg_to_string(&alg);
        printf("Applied: %s\n", str ? str : p);
        free(str);
        alg_free(&alg);

        print_cube(&cube);
    }

    return 0;
}
