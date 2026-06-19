/*
 * pipeline.c — end-to-end test: CCF -> CCS.
 *
 * Runs the full abstract architecture search (CCF), selects the simplest
 * single-register order-3 architecture, then runs CCS to find a concrete
 * algorithm.
 *
 * Known limitations (pending fixes):
 *   - build_target uses a uniform ori_delta shift that is geometrically
 *     wrong for some corner-to-corner transitions.  Only ~5/56 corner
 *     3-cycle assignments find a valid algorithm; the rest are silently
 *     skipped.  Fix: replace the uniform shift with a face-based sticker
 *     mapping derived from PIECE_FACELETS geometry.
 *   - ccs_find_alg only finds algorithms up to BFS_FWD_DEPTH + BWD_DEPTH_LIMIT
 *     = 10 moves; longer algorithms return false.
 *   - ccf_run() is declared in ccf.h but not yet implemented; we call the
 *     step-level functions directly.
 */

#include "../include/ccf.h"
#include "../include/ccs.h"
#include "../include/cube.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_ORDER_SPECS 256

int main(void) {
    cube_init();

    /* ── Step 1: CCF ─────────────────────────────────────────────── */
    printf("=== CCF: abstract architecture search ===\n");

    CCFOrderSpec specs[MAX_ORDER_SPECS];
    int nspecs = ccf_order_specs(CCF_PRIME_POWERS, CCF_MAX_PRIME_POWERS,
                                 specs, MAX_ORDER_SPECS);
    printf("  %d achievable register orders\n", nspecs);

    CCFResult ccf = {0};
    ccf_find_architectures(specs, nspecs, &ccf);
    printf("  %d non-redundant architectures\n\n", ccf.count);
    ccf_dump(&ccf, stdout);

    /* ── Step 2: pick the simplest concrete target ───────────────── */
    /*
     * Ask for a single register of order 3 — the minimal interesting case
     * (a corner 3-cycle, order 3, no net twist).  This is the case we've
     * already validated: ccs_solve finds 5 valid concrete architectures in
     * ~13s with the current build_target implementation.
     */
    printf("\n=== Selecting order-3 single-register architecture ===\n");
    const int want[] = {3};
    const CCFArchitecture *chosen = ccf_select(&ccf, want, 1, 0, 0);
    if (!chosen) {
        fprintf(stderr, "No order-3 single-register architecture in CCF output.\n");
        ccf_free(&ccf);
        return 1;
    }

    printf("  %d register(s)", chosen->num_registers);
    for (int r = 0; r < chosen->num_registers; r++) {
        const CCFRegister *reg = &chosen->registers[r];
        printf("  reg[%d] order=%d (", r, reg->order);
        for (int c = 0; c < reg->num_cycles; c++) {
            const CCFCycle *cy = &reg->cycles[c];
            printf("%s%s len=%d ori=%+d",
                   c ? ", " : "",
                   cy->orbit == 0 ? "corner" : "edge",
                   cy->length, cy->net_orientation);
        }
        printf(")");
    }
    printf("\n  free_corners=%d  free_edges=%d\n\n",
           chosen->free_corners, chosen->free_edges);

    /* ── Step 3: CCS solve ───────────────────────────────────────── */
    printf("=== CCS: concrete algorithm search ===\n");
    printf("(NOTE: build_target ori_delta bug — expect ~5/56 assignments to\n");
    printf(" succeed; the other 51 have geometrically inconsistent targets)\n\n");
    fflush(stdout);

    clock_t t0 = clock();
    CCSResult ccs = {0};
    ccs_solve(chosen, &ccs);
    clock_t t1 = clock();

    printf("Done in %.2fs — found %d concrete architecture(s)\n",
           (double)(t1 - t0) / CLOCKS_PER_SEC, ccs.count);

    const CCSArchitecture *best = ccs_best(&ccs);
    if (best) {
        printf("\nBest concrete architecture:\n");
        ccs_dump(best, stdout);
        printf("Valid: %s\n", ccs_verify(best) ? "YES" : "NO");
    } else {
        printf("No concrete architecture found.\n");
    }

    ccs_free(&ccs);
    ccf_free(&ccf);
    return 0;
}
