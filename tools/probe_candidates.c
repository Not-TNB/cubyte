/*
 * Probe every REG_CANDIDATES entry: attempt ccs_find_alg and report
 * which orders Kociemba can concretely realise.
 *
 * Build (from repo root):
 *   cc -std=c17 -O2 -Iinclude \
 *      -DCKOCIEMBA_BIN='"third_party/ckociemba/bin/kociemba"' \
 *      -DCKOCIEMBA_CACHE='"third_party/ckociemba/cprunetables"' \
 *      tools/probe_candidates.c src/ccf.c src/ccs.c src/cube.c src/alg.c \
 *      src/util.c src/kociemba.c -o tools/probe_candidates
 */
#include "../include/ccf.h"
#include "../include/ccs.h"
#include "../include/cube.h"
#include "../include/alg.h"
#include "../include/piece.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) { return (double)clock() * 1000.0 / CLOCKS_PER_SEC; }

static const PieceLabel CORNERS[] = {
    PC_UFL, PC_UFR, PC_UBL, PC_UBR, PC_DFL, PC_DFR, PC_DBL, PC_DBR,
};
static const PieceLabel EDGES[] = {
    PC_UF, PC_UB, PC_UL, PC_UR, PC_DF, PC_DB, PC_DL, PC_DR,
    PC_FL, PC_FR, PC_BL, PC_BR,
};

static int igcd(int a, int b) { while (b) { int t=a%b; a=b; b=t; } return a; }

static int cycle_order(int orbit, int length, int twist) {
    int period = (orbit == 0) ? 3 : 2;
    return length * (period / igcd(twist == 0 ? period : twist, period));
}

int main(void) {
    cube_init();

    CCFTemplate templates[2000];
    int n = ccf_enumerate_templates(templates, 2000);

    int ok = 0, fail = 0;
    printf("%-6s %-7s %-8s %-6s  %s\n",
           "order", "pieces", "result", "ms", "algorithm");
    printf("%-6s %-7s %-8s %-6s  %s\n",
           "------","-------","--------","------","---");

    for (int t = 0; t < n; t++) {
        const CCFTemplate *tmpl = &templates[t];

        /* Build a minimal CCSRegister from the template (first available pieces). */
        CCSRegister reg = {0};
        reg.abstract.order     = tmpl->order;
        reg.abstract.num_cycles = tmpl->num_cycles;

        int cp = 0, ep = 0;
        int total_pieces = 0;
        int bad = 0;
        for (int c = 0; c < tmpl->num_cycles; c++) {
            const CCFCycle *cy = &tmpl->cycles[c];
            reg.abstract.cycles[c] = *cy;
            reg.cycles[c].abstract = *cy;

            const PieceLabel *pool = (cy->orbit == 0) ? CORNERS : EDGES;
            int *pos = (cy->orbit == 0) ? &cp : &ep;
            int pool_max = (cy->orbit == 0) ? 8 : 12;

            if (*pos + cy->length > pool_max) { bad = 1; break; }
            for (int j = 0; j < cy->length; j++)
                reg.cycles[c].pieces[j] = pool[(*pos)++];

            total_pieces += cy->length;

            int f = (cy->orbit == 0) ? 3 : 2;
            int no = ((cy->net_orientation % f) + f) % f;
            for (int j = 0; j < cy->length; j++)
                reg.cycles[c].ori_deltas[j] = 0;
            if (no != 0)
                reg.cycles[c].ori_deltas[0] = (int8_t)no;
        }

        if (bad) {
            printf("order=%-5d  SKIP (piece budget exceeded)\n", tmpl->order);
            fail++;
            continue;
        }

        double t0 = now_ms();
        Alg alg = {0};
        int solved = ccs_find_alg(&reg, &alg);
        double elapsed = now_ms() - t0;

        if (solved) {
            char *s = alg_to_string(&alg);
            printf("%-6d %-7d %-8s %-6.0f  %s\n",
                   tmpl->order, total_pieces, "OK", elapsed, s);
            free(s);
            alg_free(&alg);
            ok++;
        } else {
            printf("%-6d %-7d %-8s %-6.0f\n",
                   tmpl->order, total_pieces, "FAIL", elapsed);
            fail++;
        }
    }

    printf("\n%d / %d templates realised by Kociemba\n", ok, ok + fail);
    return 0;
}
