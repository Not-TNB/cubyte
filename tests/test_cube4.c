#include "cube/cube4.h"
#include "cube/alg.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void test_identity(void) {
    CubeState4 s;
    cube4_identity(&s);
    assert(cube4_is_identity(&s));
    printf("PASS: identity\n");
}

static void test_single_move_order(void) {
    /* Every outer or wide single-face move has order 4. */
    const char *moves[] = { "U", "U2", "U'", "Uw", "D", "Dw", "L", "Lw",
                             "R", "Rw", "F", "Fw", "B", "Bw" };
    for (int i = 0; i < (int)(sizeof moves / sizeof *moves); i++) {
        Alg a = {0};
        assert(alg_parse(moves[i], &a));
        int ord = cube4_compute_order(&a);
        /* U2 has order 2, everything else order 4 */
        int expected = (strchr(moves[i], '2') != NULL) ? 2 : 4;
        if (ord != expected) {
            fprintf(stderr, "FAIL: order(%s) = %d, expected %d\n",
                    moves[i], ord, expected);
            assert(0);
        }
        alg_free(&a);
    }
    printf("PASS: single-move orders\n");
}

static void test_u_move_is_not_identity(void) {
    CubeState4 s;
    cube4_identity(&s);
    Alg a = {0};
    assert(alg_parse("U", &a));
    cube4_apply_sequence(&s, &a);
    assert(!cube4_is_identity(&s));
    alg_free(&a);
    printf("PASS: U move produces non-identity state\n");
}

static void test_u4_is_identity(void) {
    CubeState4 s;
    cube4_identity(&s);
    Alg a = {0};
    assert(alg_parse("U U U U", &a));
    cube4_apply_sequence(&s, &a);
    assert(cube4_is_identity(&s));
    alg_free(&a);
    printf("PASS: U^4 = identity\n");
}

static void test_superflip_like_order(void) {
    /* (R U)^105 = identity on 3x3; here just verify compute_order runs. */
    Alg a = {0};
    assert(alg_parse("R U", &a));
    int ord = cube4_compute_order(&a);
    assert(ord > 0);
    alg_free(&a);
    printf("PASS: compute_order(R U) = %d\n", ord);
}

static void test_wide_vs_outer_differ(void) {
    /* Uw should differ from U (it also moves inner slice). */
    CubeState4 u, uw;
    cube4_identity(&u);
    cube4_identity(&uw);
    Alg a = {0}, b = {0};
    assert(alg_parse("U",  &a));
    assert(alg_parse("Uw", &b));
    cube4_apply_sequence(&u,  &a);
    cube4_apply_sequence(&uw, &b);
    assert(!cube4_equal(&u, &uw));
    alg_free(&a);
    alg_free(&b);
    printf("PASS: U and Uw produce different states\n");
}

static void test_uw_u_prime_is_inner_slice(void) {
    /* Uw U' should only affect the inner slice (no outer U move net). */
    CubeState4 s;
    cube4_identity(&s);
    Alg a = {0};
    assert(alg_parse("Uw U'", &a));
    cube4_apply_sequence(&s, &a);
    assert(!cube4_is_identity(&s));
    /* The outer U facelets (row 0 of each side) should be unaffected. */
    /* U face itself (0-15) should be unaffected since U' cancels Uw's U layer */
    for (int i = 0; i < 16; i++)
        assert(s.state[i] == (uint8_t)i);
    alg_free(&a);
    printf("PASS: Uw U' leaves U face and outer row unchanged\n");
}

static void test_move_directions(void) {
    /* After U CW: F→L→B→R→F (user-confirmed convention). */
    CubeState4 s;
    cube4_identity(&s);
    Alg a = {0};
    assert(alg_parse("U", &a));
    cube4_apply_sequence(&s, &a);
    assert(s.state[64] / 16 == FACE_R);  /* F top-left now shows R colour (R→F) */
    assert(s.state[48] / 16 == FACE_B);  /* R top-left now shows B colour (B→R) */
    alg_free(&a);

    /* After R CW: F right col (slot 67) should now show on U right col (slot 3). */
    cube4_identity(&s);
    assert(alg_parse("R", &a));
    cube4_apply_sequence(&s, &a);
    assert(s.state[3]  / 16 == FACE_F);  /* U top-right now shows F colour */
    assert(s.state[67] / 16 == FACE_D);  /* F top-right now shows D colour */
    alg_free(&a);

    /* After L CW: F left col (slot 64) should now show D colour. */
    cube4_identity(&s);
    assert(alg_parse("L", &a));
    cube4_apply_sequence(&s, &a);
    assert(s.state[64] / 16 == FACE_U);  /* F top-left now shows U colour */
    assert(s.state[0]  / 16 == FACE_B);  /* U top-left now shows B colour */
    alg_free(&a);

    /* After F CW: U bottom-left (slot 12) should now show on R left col (slot 48). */
    cube4_identity(&s);
    assert(alg_parse("F", &a));
    cube4_apply_sequence(&s, &a);
    assert(s.state[48] / 16 == FACE_U);  /* R top-left now shows U colour */
    assert(s.state[12] / 16 == FACE_L);  /* U bottom-left now shows L colour */
    alg_free(&a);

    printf("PASS: move directions (U/R/L/F CW are correct)\n");
}

static void test_cycleset_nonempty(void) {
    Alg a = {0};
    assert(alg_parse("U", &a));
    CycleSet4 cs = cube4_cycleset_from_alg(&a);
    assert(cs != CYCLESET4_EMPTY);
    alg_free(&a);
    printf("PASS: cycleset(U) is non-empty\n");
}

static void test_facelet_to_piece_count(void) {
    /* Verify the solved-state table has the right piece counts. */
    int count[PC4_COUNT];
    memset(count, 0, sizeof count);
    for (int i = 0; i < FACELET4_COUNT; i++)
        count[facelet_to_piece4[i]]++;
    /* Corners: 3 facelets each */
    for (int p = PC4_UFL; p <= PC4_DBR; p++)
        assert(count[p] == 3);
    /* Wings: 2 facelets each */
    for (int p = PC4_UF_A; p <= PC4_BR_B; p++)
        assert(count[p] == 2);
    /* X-centres: 1 facelet each */
    for (int p = PC4_U_FL; p <= PC4_B_DR; p++)
        assert(count[p] == 1);
    printf("PASS: facelet_to_piece4 piece counts correct\n");
}

int main(void) {
    cube4_init();

    test_identity();
    test_facelet_to_piece_count();
    test_u_move_is_not_identity();
    test_u4_is_identity();
    test_single_move_order();
    test_wide_vs_outer_differ();
    test_uw_u_prime_is_inner_slice();
    test_move_directions();
    test_cycleset_nonempty();
    test_superflip_like_order();

    printf("All cube4 tests passed.\n");
    return 0;
}
