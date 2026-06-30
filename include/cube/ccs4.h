#ifndef CCS4_H
#define CCS4_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include "ccf4.h"
#include "cube4.h"
#include "alg.h"
#include "piece4.h"

#define CCS4_MAX_CYCLE_LEN 24  /* max wing-edge cycle on the 4×4 */

typedef struct {
    CCF4Cycle  abstract;
    PieceLabel4 pieces[CCS4_MAX_CYCLE_LEN];
    int8_t      ori_deltas[CCS4_MAX_CYCLE_LEN];
} CCS4Cycle;

typedef struct {
    CCF4Register abstract;
    CCS4Cycle    cycles[CCF4_MAX_CYCLES_PER_REG];
    Alg          alg;
    CycleSet4    cycleset;
} CCS4Register;

typedef struct {
    const CCF4Architecture *abstract;
    CCS4Register            regs[CCF4_MAX_REGISTERS];
    int                     num_regs;
    CycleSet4               free_pieces;
} CCS4Architecture;

typedef struct {
    CCS4Architecture *archs;
    int               count;
    int               cap;
} CCS4Result;

void                    ccs4_solve(const CCF4Architecture *arch, CCS4Result *out);
void                    ccs4_free(CCS4Result *result);
const CCS4Architecture *ccs4_best(const CCS4Result *result);
bool                    ccs4_verify(const CCS4Architecture *arch);
void                    ccs4_dump(const CCS4Architecture *arch, FILE *fp);

#endif /* CCS4_H */
