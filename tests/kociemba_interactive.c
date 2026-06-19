#include "../include/kociemba.h"
#include "../include/alg.h"
#include "../include/cube.h"
#include "../include/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int move_count(const char *s) {
    if (!s || !s[0]) return 0;
    int n = 1;
    for (const char *p = s; *p; p++)
        if (*p == ' ') n++;
    return n;
}

static void run(const char *alg_str) {
    int before = move_count(alg_str);
    char *result = kociemba_shorten(alg_str);

    if (result == NULL) {
        fprintf(stderr, "error: kociemba_shorten failed (malformed input or solver error)\n");
        return;
    }

    int after = move_count(result);
    printf("input  (%2d): %s\n", before, alg_str[0] ? alg_str : "(identity)");
    printf("output (%2d): %s\n", after,  result[0]  ? result  : "(identity)");
    if (before > 0 && after < before)
        printf("saved: %d moves\n", before - after);
    free(result);
}

int main(int argc, char **argv) {
    cube_init();

    if (argc >= 2) {
        /* Concatenate all args into one algorithm string */
        size_t total = 0;
        for (int i = 1; i < argc; i++) total += strlen(argv[i]) + 1;
        char *joined = malloc(total + 1);
        joined[0] = '\0';
        for (int i = 1; i < argc; i++) {
            if (i > 1) strcat(joined, " ");
            strcat(joined, argv[i]);
        }
        run(joined);
        free(joined);
        return 0;
    }

    /* Interactive REPL */
    printf("kociemba shortener — enter a move sequence (or q to quit)\n");
    char line[1024];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == 'q' && line[1] == '\0') break;
        if (line[0] == '\0') continue;
        run(line);
    }
    return 0;
}
