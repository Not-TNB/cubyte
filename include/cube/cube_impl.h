#ifndef CUBE_IMPL_H
#define CUBE_IMPL_H

#include <stdint.h>
#include <stdbool.h>
#include "alg.h"

/*
 * CubeImpl — polymorphic interface over the 3×3 and 4×4 backends.
 *
 * Callers (regalloc, ccs, ccf) accept a const CubeImpl * and call through
 * this vtable instead of calling cube3_* or cube4_* functions directly.
 * State pointers are void* to avoid pulling in cube3.h / cube4.h at every
 * call site; callers that need type safety include the concrete header too.
 */
typedef struct CubeImpl {
    int facelet_count;  /* 48 (3×3) or 96 (4×4) */
    int piece_count;    /* 20 (3×3) or 56 (4×4) */
    int state_size;     /* sizeof(CubeState3) or sizeof(CubeState4) */
    int max_order;      /* 1260 (3×3) or MAX_4X4_ORDER (4×4) */

    void (*init)(void);

    void (*identity)(void *s);
    bool (*is_identity)(const void *s);
    void (*copy)(void *dst, const void *src);
    bool (*equal)(const void *a, const void *b);
    void (*apply_sequence)(void *s, const Alg *a);

    int      (*compute_order)(const Alg *a);
    uint64_t (*cycleset_from_alg)(const Alg *a);
    bool     (*cyclesets_disjoint)(uint64_t a, uint64_t b);
    uint64_t (*cycleset_union)(uint64_t a, uint64_t b);
    void     (*cycleset_print)(uint64_t cs);

    /* Find an algorithm whose application to the identity cube produces *target.
     * target is a void* pointing to a CubeState3 or CubeState4.
     * On success fills *out (caller must alg_free) and returns true. */
    bool (*find_alg)(const void *target, Alg *out);
} CubeImpl;

/* Return the singleton vtable for each cube size.
 * cube_impl_3x3/4x4 call the corresponding cube_init lazily. */
const CubeImpl *cube_impl_3x3(void);
const CubeImpl *cube_impl_4x4(void);

#endif /* CUBE_IMPL_H */
