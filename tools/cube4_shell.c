/*
 * cube4_shell — interactive 4×4 Rubik's cube testing REPL.
 *
 * Commands:
 *   <alg>          apply any valid SiGN algorithm (e.g. "U Rw2 F'")
 *   reset          return to solved state, clear history
 *   undo           undo the last applied algorithm token
 *   history        print the sequence applied so far
 *   order          print the order of the current cumulative sequence
 *   order <alg>    print the order of a specific algorithm
 *   cycles         print the piece cycle-set of the current sequence
 *   solved         report whether the cube is solved
 *   help           list commands
 *   quit / exit    exit
 */

#include "cube/cube4.h"
#include "cube/alg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ---------------------------------------------------------------------------
 * ANSI colour display
 * ---------------------------------------------------------------------------
 * Each facelet's colour is determined by s->state[i] / 16 (its home face in
 * the solved state), since s->state[i] is the solved-state slot currently
 * occupying position i.
 *
 * Face 0 U = white   Face 1 D = yellow  Face 2 L = orange
 * Face 3 R = red     Face 4 F = green   Face 5 B = blue
 */

static const char *const FACE_ANSI[6] = {
    "\033[107m",        /* U: bright white  */
    "\033[103m",        /* D: bright yellow */
    "\033[48;5;208m",   /* L: orange (256-colour) */
    "\033[101m",        /* R: bright red    */
    "\033[102m",        /* F: bright green  */
    "\033[104m",        /* B: bright blue   */
};
static const char *const FACE_LABEL[6] = { "U","D","L","R","F","B" };
#define RESET "\033[0m"

/* Print one sticker (2 coloured spaces). */
static void print_sticker(const CubeState4 *s, int slot) {
    int face = s->state[slot] / 16;
    printf("%s  %s", FACE_ANSI[face], RESET);
}

/* Horizontal separator for one n-cell wide face-band. */
static void print_hbar(int cells) {
    for (int i = 0; i < cells; i++) printf("+--");
    printf("+");
}

/* Print a single row of 4 stickers from face base+row*4. */
static void print_face_row(const CubeState4 *s, int base, int row) {
    printf("|");
    for (int c = 0; c < 4; c++) {
        print_sticker(s, base + row * 4 + c);
        printf("|");
    }
}

/*
 * Net layout (each □ = one sticker):
 *
 *          U(0-15)
 *  L(32-47) F(64-79) R(48-63) B(80-95)
 *          D(16-31)
 */
static void print_net(const CubeState4 *s) {
    const int INDENT = 20; /* 4 cells × 5 chars/cell = 20 */

    /* ── U face ── */
    for (int r = 0; r < 4; r++) {
        printf("%*s", INDENT, "");
        print_hbar(4);
        printf("\n%*s", INDENT, "");
        print_face_row(s, 0, r);
        printf("\n");
    }
    printf("%*s", INDENT, "");
    print_hbar(4);
    printf("\n");

    /* ── middle band: L F R B ── */
    for (int r = 0; r < 4; r++) {
        /* separator */
        print_hbar(4); print_hbar(4); print_hbar(4); print_hbar(4);
        printf("\n");
        /* row */
        print_face_row(s, 32, r);
        print_face_row(s, 64, r);
        print_face_row(s, 48, r);
        print_face_row(s, 80, r);
        printf("\n");
    }
    print_hbar(4); print_hbar(4); print_hbar(4); print_hbar(4);
    printf("\n");

    /* ── D face ── */
    for (int r = 0; r < 4; r++) {
        printf("%*s", INDENT, "");
        print_face_row(s, 16, r);
        printf("\n%*s", INDENT, "");
        print_hbar(4);
        printf("\n");
    }

    /* Face legend */
    printf("\n");
    printf("  %*s%-8s%-8s%-8s%-8s%-8s%-8s%s\n",
           INDENT - 2, "",
           "U=white", "D=yellow", "L=orange", "R=red", "F=green", "B=blue",
           "");
    for (int f = 0; f < 6; f++) {
        printf("  %s %s %s  ", FACE_ANSI[f], FACE_LABEL[f], RESET);
    }
    printf("\n\n");
}

/* ---------------------------------------------------------------------------
 * History
 * ---------------------------------------------------------------------------
 * We keep an array of Alg tokens (one per user input line that was a move).
 * Undo pops the last one and rebuilds from scratch.
 */
#define MAX_HISTORY 256

static Alg   history_algs[MAX_HISTORY];
static int   history_len = 0;
static CubeState4 current;

static void rebuild_state(void) {
    cube4_identity(&current);
    for (int i = 0; i < history_len; i++)
        cube4_apply_sequence(&current, &history_algs[i]);
}

static void push_alg(Alg *a) {
    if (history_len >= MAX_HISTORY) {
        fprintf(stderr, "History full (max %d).\n", MAX_HISTORY);
        alg_free(a);
        return;
    }
    history_algs[history_len++] = *a;
    /* *a is now owned by history; do not call alg_free on it here */
}

static void print_history(void) {
    if (history_len == 0) { printf("(empty)\n"); return; }
    for (int i = 0; i < history_len; i++) {
        char *s = alg_to_string(&history_algs[i]);
        printf("%s", s);
        if (i < history_len - 1) printf(" | ");
        free(s);
    }
    printf("\n");
}

/* ---------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------------
 */

static void do_reset(void) {
    for (int i = 0; i < history_len; i++) alg_free(&history_algs[i]);
    history_len = 0;
    cube4_identity(&current);
    printf("Reset to solved state.\n\n");
    print_net(&current);
}

static void do_undo(void) {
    if (history_len == 0) { printf("Nothing to undo.\n"); return; }
    alg_free(&history_algs[--history_len]);
    rebuild_state();
    printf("Undone.\n\n");
    print_net(&current);
}

static void do_order_of_current(void) {
    /* Build one concatenated alg from history */
    Alg full = {0};
    for (int i = 0; i < history_len; i++) alg_concat(&full, &history_algs[i]);
    if (full.len == 0) { printf("Order: 1 (identity)\n"); alg_free(&full); return; }
    int ord = cube4_compute_order(&full);
    printf("Order: %d\n", ord);
    alg_free(&full);
}

static void do_order_of(const char *alg_text) {
    Alg a = {0};
    if (!alg_parse(alg_text, &a)) { printf("Parse error: %s\n", alg_text); return; }
    int ord = cube4_compute_order(&a);
    printf("Order of \"%s\": %d\n", alg_text, ord);
    alg_free(&a);
}

static void do_cycles(void) {
    Alg full = {0};
    for (int i = 0; i < history_len; i++) alg_concat(&full, &history_algs[i]);
    if (full.len == 0) { printf("Cycle set: {} (identity)\n"); alg_free(&full); return; }
    CycleSet4 cs = cube4_cycleset_from_alg(&full);
    printf("Cycle set: ");
    cycleset4_print(cs);
    printf("\n");
    alg_free(&full);
}

static void do_help(void) {
    printf(
        "Commands:\n"
        "  <alg>          apply algorithm in SiGN notation (e.g. \"U Rw2 F'\")\n"
        "                 Supported moves: U D L R F B (outer) and Uw Dw Lw Rw Fw Bw (wide)\n"
        "  reset          return to solved state\n"
        "  undo           undo last move input\n"
        "  history        show applied moves\n"
        "  order          order of the full sequence applied so far\n"
        "  order <alg>    order of a specific algorithm\n"
        "  cycles         piece cycle-set of the current sequence\n"
        "  solved         check if cube is solved\n"
        "  help           this message\n"
        "  quit / exit    exit\n"
    );
}

/* Trim leading/trailing whitespace in-place; return pointer into s. */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

/* Case-insensitive prefix match. */
static int startswith_ci(const char *haystack, const char *needle) {
    size_t n = strlen(needle);
    return strncasecmp(haystack, needle, n) == 0;
}

/* ---------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------------
 */
int main(void) {
    cube4_init();
    cube4_identity(&current);

    printf("cube4_shell — 4×4 Rubik's cube REPL\n");
    printf("Type \"help\" for commands.\n\n");
    print_net(&current);

    char line[1024];
    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof line, stdin)) {
            printf("\n");
            break;
        }

        char *cmd = trim(line);
        if (*cmd == '\0') continue;

        /* ---- built-in commands ---- */
        if (strcasecmp(cmd, "quit") == 0 || strcasecmp(cmd, "exit") == 0) {
            break;
        } else if (strcasecmp(cmd, "reset") == 0) {
            do_reset();
        } else if (strcasecmp(cmd, "undo") == 0) {
            do_undo();
        } else if (strcasecmp(cmd, "history") == 0) {
            print_history();
        } else if (strcasecmp(cmd, "solved") == 0) {
            printf(cube4_is_identity(&current) ? "Solved!\n" : "Not solved.\n");
        } else if (strcasecmp(cmd, "cycles") == 0) {
            do_cycles();
        } else if (strcasecmp(cmd, "order") == 0) {
            do_order_of_current();
        } else if (startswith_ci(cmd, "order ")) {
            do_order_of(trim(cmd + 6));
        } else if (strcasecmp(cmd, "help") == 0) {
            do_help();
        } else {
            /* Try to parse as a move sequence */
            Alg a = {0};
            if (!alg_parse(cmd, &a)) {
                printf("Unknown command or parse error: %s\n", cmd);
                printf("Type \"help\" for available commands.\n");
                continue;
            }
            cube4_apply_sequence(&current, &a);
            push_alg(&a);
            print_net(&current);
        }
    }

    /* cleanup */
    for (int i = 0; i < history_len; i++) alg_free(&history_algs[i]);
    return 0;
}
