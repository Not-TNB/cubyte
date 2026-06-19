#include "../include/kociemba.h"
#include "../include/cube.h"
#include "../include/alg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- minimal test harness ------------------------------------------------- */

static int g_passed = 0;
static int g_failed = 0;

#define ASSERT_TRUE(expr)                                                      \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);   \
            g_failed++;                                                        \
        } else {                                                               \
            g_passed++;                                                        \
        }                                                                      \
    } while (0)

#define ASSERT_NULL(p)     ASSERT_TRUE((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT_TRUE((p) != NULL)
#define ASSERT_STREQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0)

/* Apply alg string to a fresh solved cube; return the resulting CubeState. */
static CubeState apply_str(const char *alg_str) {
    Alg a = {0};
    if (!alg_parse(alg_str, &a)) {
        fprintf(stderr, "apply_str: alg_parse failed for \"%s\"\n", alg_str);
        exit(1);
    }
    CubeState s;
    cube_identity(&s);
    cube_apply_sequence(&s, &a);
    alg_free(&a);
    return s;
}

/* --- test 1: already-solved input returns empty string -------------------- */
static void test_identity_empty(void) {
    char *r = kociemba_shorten("");
    ASSERT_NOT_NULL(r);
    ASSERT_STREQ(r, "");
    free(r);
}

/* An alg that cancels itself (R R' is identity) should also return "". */
static void test_identity_cancels(void) {
    char *r = kociemba_shorten("R R'");
    ASSERT_NOT_NULL(r);
    ASSERT_STREQ(r, "");
    free(r);
}

/* --- test 2: malformed input returns NULL --------------------------------- */
static void test_malformed_null(void) {
    ASSERT_NULL(kociemba_shorten("Z Q X"));
    ASSERT_NULL(kociemba_shorten("U R U' R' @"));
}

/* --- test 3: round-trip equivalence --------------------------------------- */
/*
 * For each algorithm, kociemba_shorten must return a sequence that leaves the
 * cube in exactly the same state as the original. Length reduction is a bonus;
 * equivalence is mandatory.
 */
static void check_round_trip(const char *alg_str) {
    CubeState original = apply_str(alg_str);

    char *shortened = kociemba_shorten(alg_str);
    if (shortened == NULL) {
        fprintf(stderr, "FAIL round_trip: kociemba_shorten returned NULL for \"%s\"\n", alg_str);
        g_failed++;
        return;
    }

    CubeState via_short = apply_str(shortened);
    free(shortened);

    if (!cube_equal(&original, &via_short)) {
        fprintf(stderr, "FAIL round_trip: state mismatch for \"%s\"\n", alg_str);
        g_failed++;
    } else {
        g_passed++;
    }
}

static void test_round_trip(void) {
    check_round_trip("R U R' U'");
    check_round_trip("R U R' U' R U R' U' R U R' U'");   /* sexy move x3 */
    check_round_trip("F R U R' U' F'");                   /* OLL */
    check_round_trip("R U2 R' U' R U' R'");               /* sune */
    check_round_trip("M2 U M2 U2 M2 U M2");               /* Z-perm (slice moves) */
    check_round_trip("R U R' U R U2 R'");
    check_round_trip("U R U' L' U R' U' L");              /* T-perm-adjacent */
}

/* --- test 4: shortening sanity ------------------------------------------- */
/*
 * A long redundant algorithm should come out no longer than it went in.
 * We measure in move count by counting spaces+1 in the string.
 */
static int move_count(const char *alg_str) {
    if (!alg_str || alg_str[0] == '\0') return 0;
    int n = 1;
    for (const char *p = alg_str; *p; p++)
        if (*p == ' ') n++;
    return n;
}

static void check_not_longer(const char *alg_str) {
    int original_len = move_count(alg_str);
    char *shortened = kociemba_shorten(alg_str);
    if (shortened == NULL) {
        fprintf(stderr, "FAIL shortening: NULL for \"%s\"\n", alg_str);
        g_failed++;
        return;
    }
    int short_len = move_count(shortened);
    free(shortened);

    if (short_len > original_len) {
        fprintf(stderr, "FAIL shortening: %d moves -> %d moves for \"%s\"\n",
                original_len, short_len, alg_str);
        g_failed++;
    } else {
        g_passed++;
    }
}

static void test_shortening(void) {
    /* 6-move alg repeated 6 times = 36 moves; Kociemba must beat that easily */
    check_not_longer("R U R' U' R U R' U' R U R' U' R U R' U' R U R' U' R U R' U'");
    /* 4-move sune repeated 5 times */
    check_not_longer("R U2 R' U' R U' R' R U2 R' U' R U' R' R U2 R' U' R U' R' R U2 R' U' R U' R' R U2 R' U' R U' R'");
}

/* --- test 5: facelet translation (solved state sanity) -------------------- */
/*
 * Applying no moves and calling kociemba_shorten on the empty-string identity
 * should return "" (not NULL, not garbage). Already covered by test 1, but we
 * also verify that single-move inputs survive the translation without crashing
 * and produce a valid (non-NULL) result.
 */
static void test_single_moves(void) {
    const char *moves[] = {"U", "D", "L", "R", "F", "B",
                           "U'", "D'", "L'", "R'", "F'", "B'",
                           "U2", "D2", "L2", "R2", "F2", "B2", NULL};
    for (int i = 0; moves[i]; i++) {
        char *r = kociemba_shorten(moves[i]);
        if (r == NULL) {
            fprintf(stderr, "FAIL single_moves: NULL for \"%s\"\n", moves[i]);
            g_failed++;
        } else {
            /* Result must be a valid alg that reproduces the same state */
            CubeState original = apply_str(moves[i]);
            CubeState via_short = apply_str(r);
            if (!cube_equal(&original, &via_short)) {
                fprintf(stderr, "FAIL single_moves: state mismatch for \"%s\" -> \"%s\"\n",
                        moves[i], r);
                g_failed++;
            } else {
                g_passed++;
            }
            free(r);
        }
    }
}

/* -------------------------------------------------------------------------- */

int main(void) {
    printf("=== kociemba tests ===\n");

    test_identity_empty();
    test_identity_cancels();
    test_malformed_null();
    test_round_trip();
    test_shortening();
    test_single_moves();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
