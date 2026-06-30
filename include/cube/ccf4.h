#ifndef CCF4_H
#define CCF4_H

#include <stdint.h>
#include <stdio.h>
#include "cube4.h"
#include "piece4.h"

/* 4×4 cube constants. */
#define CCF4_CORNER_COUNT   8
#define CCF4_WING_COUNT    24
#define CCF4_CENTRE_COUNT  24
#define CCF4_PIECE_COUNT   (CCF4_CORNER_COUNT + CCF4_WING_COUNT + CCF4_CENTRE_COUNT)  /* 56 */

#define CCF4_CORNER_PERIOD  3  /* orientation period for corners */
#define CCF4_WING_PERIOD    2  /* orientation period for wing edges */
#define CCF4_CENTRE_PERIOD  1  /* centres have no orientation twist */

#define CCF4_MAX_CYCLES_PER_REG 10
#define CCF4_MAX_REGISTERS       8

typedef struct {
    uint8_t orbit;           /* 0=corners, 1=wings, 2=centres */
    uint8_t length;
    int8_t  net_orientation;
    int     order;
} CCF4Cycle;

typedef struct {
    CCF4Cycle cycles[CCF4_MAX_CYCLES_PER_REG];
    int       num_cycles;
    int       order;
} CCF4Register;

typedef struct {
    CCF4Register registers[CCF4_MAX_REGISTERS];
    int          num_registers;
    int          free_corners;
    int          free_wings;
    int          free_centres;
} CCF4Architecture;

typedef struct {
    CCF4Architecture *archs;
    int               count;
    int               cap;
} CCF4Result;

void ccf4_run(CCF4Result *out);
void ccf4_free(CCF4Result *result);
const CCF4Architecture *ccf4_select(const CCF4Result *result,
                                    const int *required_orders, int count,
                                    int bias_lo, int bias_hi);
void ccf4_dump(const CCF4Result *result, FILE *fp);

#endif /* CCF4_H */
