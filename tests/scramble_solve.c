#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../include/cube.h"
#include "../include/kociemba.h"
#include "../include/alg.h"

static const char *MOVES[] = {
    "U","U2","U'","D","D2","D'","L","L2","L'","R","R2","R'","F","F2","F'","B","B2","B'"
};
#define NMOVES 18

int main(void) {
    cube_init();
    srand((unsigned)time(NULL));

    /* Generate a random 20-move scramble, avoiding back-to-back same face. */
    CubeState s;
    cube_identity(&s);

    char scramble[512] = {0};
    int pos = 0;
    int prev_face = -1;
    for (int i = 0; i < 20; i++) {
        int m;
        do { m = rand() % NMOVES; } while (m / 3 == prev_face);
        prev_face = m / 3;

        Alg a = {0};
        alg_parse(MOVES[m], &a);
        cube_apply_sequence(&s, &a);
        alg_free(&a);

        if (i > 0) scramble[pos++] = ' ';
        const char *mv = MOVES[m];
        while (*mv) scramble[pos++] = *mv++;
    }
    scramble[pos] = '\0';

    printf("Scramble: %s\n", scramble);

    Alg solution = {0};
    if (!kociemba_solve_state(&s, &solution)) {
        fprintf(stderr, "Kociemba failed.\n");
        return 1;
    }

    char *sol_str = alg_to_string(&solution);
    printf("Solution: %s\n", sol_str);
    free(sol_str);
    alg_free(&solution);
    return 0;
}
