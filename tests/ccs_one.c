/* Probe ccs_find_alg on hand-built registers, including orientation cycles. */
#include "../include/ccf.h"
#include "../include/ccs.h"
#include "../include/cube.h"
#include "../include/alg.h"
#include "../include/piece.h"

#include <stdio.h>
#include <stdlib.h>

static int cyc_order(int orbit, int len, int tw) {
    int period = orbit == 0 ? 3 : 2, g = tw, b = period;
    while (b) { int t = g % b; g = b; b = t; }
    return len * (period / g);
}

/* Build a CCSRegister from a list of (orbit,len,twist) cycles, assigning
 * disjoint pieces greedily. ori_deltas put the whole twist on the last piece. */
static void build_reg(CCSRegister *reg, int order, int nc,
                      const int orbit[], const int len[], const int tw[]) {
    static const PieceLabel CORNERS[8] = {PC_UFL,PC_UFR,PC_UBL,PC_UBR,PC_DFL,PC_DFR,PC_DBL,PC_DBR};
    static const PieceLabel EDGES[12]  = {PC_UF,PC_UL,PC_UR,PC_UB,PC_DF,PC_DL,PC_DR,PC_DB,PC_FL,PC_FR,PC_BL,PC_BR};
    int ci = 0, ei = 0;
    reg->abstract.order = order;
    reg->abstract.num_cycles = nc;
    for (int c = 0; c < nc; c++) {
        CCFCycle ab = {.orbit=(uint8_t)orbit[c], .length=(uint8_t)len[c],
                       .net_orientation=(int8_t)tw[c], .order=cyc_order(orbit[c],len[c],tw[c])};
        reg->abstract.cycles[c] = ab;
        reg->cycles[c].abstract = ab;
        for (int k = 0; k < len[c]; k++)
            reg->cycles[c].pieces[k] = orbit[c]==0 ? CORNERS[ci++] : EDGES[ei++];
        for (int k = 0; k < len[c]; k++) reg->cycles[c].ori_deltas[k] = 0;
        reg->cycles[c].ori_deltas[len[c]-1] = (int8_t)tw[c]; /* put net twist on last */
    }
}

static void probe(const char *name, CCSRegister *reg) {
    Alg alg = {0};
    bool ok = ccs_find_alg(reg, &alg);
    if (ok) {
        char *s = alg_to_string(&alg);
        printf("%-28s OK order=%d len=%d \"%s\"\n", name, compute_order(&alg), alg.len, s);
        free(s); alg_free(&alg);
    } else {
        printf("%-28s FAILED\n", name);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    cube_init();

    CCSRegister r;

    /* order 2: two flipped edges */
    build_reg(&r, 2, 2, (int[]){1,1}, (int[]){1,1}, (int[]){1,1});
    probe("2: two flipped edges", &r);

    /* order 3: two twisted corners (+1,+2) */
    build_reg(&r, 3, 2, (int[]){0,0}, (int[]){1,1}, (int[]){1,2});
    probe("3: two twisted corners", &r);

    /* order 9: twisted corner + twisted 3-cycle  (C1 ori1)(C3 ori2) */
    build_reg(&r, 9, 2, (int[]){0,0}, (int[]){1,3}, (int[]){1,2});
    probe("9: C1ori1 + C3ori2", &r);

    /* corner sub-register of 1260: (C3 o1)(C5 o2) -> order lcm(9,15)=45 */
    build_reg(&r, 45, 2, (int[]){0,0}, (int[]){3,5}, (int[]){1,2});
    probe("45: C3o1 + C5o2", &r);

    /* edge sub-register of 1260: (E2 o0)(E2 o1)(E7 o1) -> lcm(2,4,14)=28 */
    build_reg(&r, 28, 3, (int[]){1,1,1}, (int[]){2,2,7}, (int[]){0,1,1});
    probe("28: E2 + E2o1 + E7o1", &r);

    /* single corner 5-cycle with twist needs a partner; test C5o1+C5o2 -> lcm(15,15)=15 */
    build_reg(&r, 15, 2, (int[]){0,0}, (int[]){5,3}, (int[]){2,1});
    probe("15: C5o2 + C3o1", &r);

    /* Registers from the failing architectures, isolated. */
    build_reg(&r, 10, 2, (int[]){1,1}, (int[]){1,5}, (int[]){1,1}); probe("10: E1o1 + E5o1", &r);
    build_reg(&r, 6,  2, (int[]){0,0}, (int[]){2,2}, (int[]){1,2}); probe("6: C2o1 + C2o2", &r);
    build_reg(&r, 30, 2, (int[]){1,1}, (int[]){3,5}, (int[]){1,1}); probe("30: E3o1 + E5o1", &r);
    build_reg(&r, 15, 2, (int[]){0,0}, (int[]){1,5}, (int[]){1,2}); probe("15: C1o1 + C5o2", &r);
    build_reg(&r, 4,  2, (int[]){1,1}, (int[]){2,2}, (int[]){1,1}); probe("4: E2o1 + E2o1", &r);

    return 0;
}
