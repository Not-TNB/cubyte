/*
 * CCS/CCF exploration harness (Kociemba-backed CCS).
 *
 * Build:
 *   cc -std=c17 -O2 -Iinclude \
 *      -DCKOCIEMBA_BIN='"third_party/ckociemba/bin/kociemba"' \
 *      -DCKOCIEMBA_CACHE='"third_party/ckociemba/cprunetables"' \
 *      tests/ccs_explore.c src/ccf.c src/ccs.c src/cube.c src/alg.c \
 *      src/util.c src/kociemba.c -o tests/ccs_explore
 *
 * Run (from repo root extension/):
 *   tests/ccs_explore                 # list CCF architectures
 *   tests/ccs_explore 3               # find + dump a register set for order(s)
 *   tests/ccs_explore 2 3 4           # multi-register architecture
 */
#include "../include/ccf.h"
#include "../include/ccs.h"
#include "../include/cube.h"
#include "../include/alg.h"
#include "../include/piece.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_ms(void) { return (double)clock() * 1000.0 / CLOCKS_PER_SEC; }

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    cube_init();

    double t0 = now_ms();
    CCFResult cff = {0};
    ccf_run(&cff);
    double t1 = now_ms();
    printf("CCF: %d architectures  (%.1f ms)\n", cff.count, t1 - t0);

    if (argc > 1) {
        int req[CCF_MAX_REGISTERS], nreq = 0;
        for (int i = 1; i < argc && nreq < CCF_MAX_REGISTERS; i++)
            req[nreq++] = atoi(argv[i]);

        const CCFArchitecture *arch = ccf_select(&cff, req, nreq, 0, 0);
        if (!arch) {
            printf("ccf_select: no architecture matches those orders\n");
            ccf_free(&cff);
            return 1;
        }
        printf("selected architecture: %d registers\n", arch->num_registers);
        for (int r = 0; r < arch->num_registers; r++) {
            const CCFRegister *reg = &arch->registers[r];
            int par = 0;
            printf("  reg[%d] order=%d cycles:", r, reg->order);
            for (int c = 0; c < reg->num_cycles; c++) {
                const CCFCycle *cy = &reg->cycles[c];
                printf(" (%s len=%d ori=%d)", cy->orbit==0?"C":"E", cy->length, cy->net_orientation);
                par ^= (cy->length - 1) & 1;
            }
            printf("  total_parity=%s\n", par ? "ODD(unreachable alone)" : "even");
        }

        double s0 = now_ms();
        CCSResult ccs = {0};
        ccs_solve(arch, &ccs);
        double s1 = now_ms();
        printf("ccs_solve: %d concrete arch(s)  (%.1f ms)\n", ccs.count, s1 - s0);

        const CCSArchitecture *best = ccs_best(&ccs);
        if (best) {
            printf("verify = %s\n", ccs_verify(best) ? "OK" : "FAIL");
            ccs_dump(best, stdout);
        }
        ccs_free(&ccs);
    } else {
        /* Scan: how many architectures have EVERY register individually
         * reachable (even perm parity, zero net corner twist, zero net edge flip)? */
        int fully_valid = 0;
        for (int i = 0; i < cff.count; i++) {
            const CCFArchitecture *a = &cff.archs[i];
            bool all_ok = true;
            for (int r = 0; r < a->num_registers && all_ok; r++) {
                const CCFRegister *reg = &a->registers[r];
                int par = 0, co = 0, eo = 0;
                for (int c = 0; c < reg->num_cycles; c++) {
                    const CCFCycle *cy = &reg->cycles[c];
                    par ^= (cy->length - 1) & 1;
                    if (cy->orbit == 0) co += cy->net_orientation; else eo += cy->net_orientation;
                }
                if (par || (((co%3)+3)%3) || (((eo%2)+2)%2)) all_ok = false;
            }
            if (all_ok) {
                fully_valid++;
                if (fully_valid <= 12) {
                    printf("  VALID arch[%d] regs=%d orders:", i, a->num_registers);
                    for (int r = 0; r < a->num_registers; r++)
                        printf(" %d", a->registers[r].order);
                    printf("\n");
                }
            }
        }
        printf("=> %d of %d architectures have all registers individually reachable\n",
               fully_valid, cff.count);
    }

    ccf_free(&cff);
    return 0;
}
