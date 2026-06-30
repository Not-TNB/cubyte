# cube4 Implementation Design

## Overview

This document details the full plan for extending CuByte to support a 4×4 Rubik's cube
as the underlying register machine. The 3×3 backend (cube3.c/h) remains the default and
is not modified. All new code lives in `src/cube/` and `include/`, with a thin vtable
(`CubeImpl`) letting the rest of the compiler stay cube-size-agnostic.

---

## 1. Piece Structure of the 4×4 Cube

### 1.1 Facelet count

A 4×4 has 6 faces × 16 stickers = **96 facelets** total. Unlike the 3×3, the four
centre stickers on each face are all moveable, so every sticker is tracked.

### 1.2 Piece types

| Type       | Count | Facelets each | Total |
|------------|-------|---------------|-------|
| Corners    |     8 |             3 |    24 |
| Wing edges |    24 |             2 |    48 |
| X-centres  |    24 |             1 |    24 |
| **Total**  |    56 |               |    96 |

**Corners** — identical to the 3×3 (UFL, UFR, UBL, UBR, DFL, DFR, DBL, DBR).

**Wing edges** — each 3×3 edge position (UF, UB, UL, UR, DF, DB, DL, DR, FL, FR, BL, BR)
carries two distinguishable wings, called `_A` (the wing closer to the first-named face)
and `_B` (the other). This gives 24 pieces: `PC4_UF_A`, `PC4_UF_B`, …, `PC4_BR_B`.

**X-centres** — each face has a 2×2 block of centre stickers. They are named by the
adjacent corner of their face: `PC4_U_FL`, `PC4_U_FR`, `PC4_U_BL`, `PC4_U_BR` (and
similarly for D, L, R, F, B). This gives 24 centre pieces.

Total: 8 + 24 + 24 = **56 pieces**.

---

## 2. New Files

```
include/
    piece4.h          — PieceLabel4 enum (56 entries + PC4_COUNT)
    cube4.h           — CubeState4, Face4 (same enum as cube3), cube4_* API
    cube_impl.h       — CubeImpl vtable, cube_impl_3x3(), cube_impl_4x4()

src/cube/
    cube4.c           — 96-slot move tables, cube4_init(), cube4_apply_move(), …
```

All other source files (`ccf`, `ccs`, `regalloc`, `codegen`, …) are touched minimally
to accept a `const CubeImpl *` instead of calling cube3 functions directly.

---

## 3. Facelet Numbering (cube4.h)

Faces are ordered by the same `Face` enum as cube3 (U=0, D=1, L=2, R=3, F=4, B=5).
Each face occupies 16 consecutive slots:

```
Face offset = face_index × 16

Within each face (row-major, top-left in standard orientation):
   0  1  2  3
   4  5  6  7
   8  9 10 11
  12 13 14 15

 → corners:      0,  3, 12, 15
 → wing edges:   1,  2,  4,  7,  8, 11, 13, 14
 → X-centres:    5,  6,  9, 10
```

So U-face = slots 0–15, D-face = 16–31, L = 32–47, R = 48–63, F = 64–79, B = 80–95.

The `facelet_to_piece4` table (built in `cube4_init`) maps each slot to its
`PieceLabel4`. It has exactly 96 entries.

---

## 4. piece4.h

```c
#ifndef PIECE4_H
#define PIECE4_H

typedef enum {
    /* Corners (0–7) — same names as piece.h PC_* */
    PC4_UFL, PC4_UFR, PC4_UBL, PC4_UBR,
    PC4_DFL, PC4_DFR, PC4_DBL, PC4_DBR,

    /* Wing edges (8–31): _A = wing closer to first face, _B = other */
    PC4_UF_A, PC4_UF_B,
    PC4_UB_A, PC4_UB_B,
    PC4_UL_A, PC4_UL_B,
    PC4_UR_A, PC4_UR_B,
    PC4_DF_A, PC4_DF_B,
    PC4_DB_A, PC4_DB_B,
    PC4_DL_A, PC4_DL_B,
    PC4_DR_A, PC4_DR_B,
    PC4_FL_A, PC4_FL_B,
    PC4_FR_A, PC4_FR_B,
    PC4_BL_A, PC4_BL_B,
    PC4_BR_A, PC4_BR_B,

    /* X-centres (32–55): named by adjacent corner of their face */
    PC4_U_FL, PC4_U_FR, PC4_U_BL, PC4_U_BR,
    PC4_D_FL, PC4_D_FR, PC4_D_BL, PC4_D_BR,
    PC4_L_UF, PC4_L_UB, PC4_L_DF, PC4_L_DB,
    PC4_R_UF, PC4_R_UB, PC4_R_DF, PC4_R_DB,
    PC4_F_UL, PC4_F_UR, PC4_F_DL, PC4_F_DR,
    PC4_B_UL, PC4_B_UR, PC4_B_DL, PC4_B_DR,

    PC4_COUNT, /* = 56 */
} PieceLabel4;

extern const char *const piece4_label_strings[PC4_COUNT];
const char *piece4_to_string(int p);

#endif /* PIECE4_H */
```

---

## 5. CycleSet4

`PC4_COUNT = 56 > 32`, so a `uint32_t` bitmask no longer fits. `CycleSet4` is `uint64_t`:

```c
typedef uint64_t CycleSet4;
#define CYCLESET4_EMPTY ((CycleSet4)0ULL)
#define CYCLESET4_FULL  ((CycleSet4)((1ULL << PC4_COUNT) - 1ULL))
```

The helper functions `cycleset4_disjoint` and `cycleset4_union` mirror the 3×3 versions.

---

## 6. cube4.h API

The public API deliberately mirrors `cube3.h` so the vtable can be thin:

```c
#define FACELET4_COUNT 96
#define MAX_4X4_ORDER  ???   /* to be determined; much larger than 1260 */

typedef struct { uint8_t state[FACELET4_COUNT]; } CubeState4;

void cube4_init(void);
void cube4_identity(CubeState4 *s);
void cube4_copy(CubeState4 *dst, const CubeState4 *src);
bool cube4_is_identity(const CubeState4 *s);
bool cube4_equal(const CubeState4 *a, const CubeState4 *b);

/* face is the same Face enum; quarter_turns in {1,2,3}; depth in {1,2} */
void cube4_apply_move(CubeState4 *s, Face face, int quarter_turns, int depth);
void cube4_apply_sequence(CubeState4 *s, const Alg *a);

int       cube4_compute_order(const Alg *a);
CycleSet4 cube4_cycleset_from_alg(const Alg *a);

extern PieceLabel4 facelet_to_piece4[FACELET4_COUNT];
```

Note: `cube4_apply_move` takes an explicit `depth` argument (1 = outer face only,
2 = outer + adjacent inner slice). The `Move.depth` field already carries this.

---

## 7. cube4.c — Move Tables

### 7.1 Structure

```c
/* MOVE_TABLE4[face][q-1][depth-1] is a 96-element permutation. */
static uint8_t MOVE_TABLE4[FACE_COUNT][3][2][FACELET4_COUNT];
```

Outer face moves (`depth=1`) permute:
- The 4 corner slots on that face (same piece geometry as cube3).
- The 8 outer wing-edge slots adjacent to that face.
- The 4 X-centre slots on that face (rotated by the face turn).
- The 3 outer slots on each of the 4 adjacent faces (12 total band stickers).

Wide moves (`depth=2`) permute everything in the outer move **plus**:
- The 4 inner wing-edge slots adjacent to that face (the inner wings of the ring).
- The 4 X-centre slots on the two adjacent inner-layer faces (the "slice" centres).

### 7.2 Piece distinguishability — design decision

**All 56 pieces are tracked individually by the compiler.** The two wings at the same
edge position (e.g., `PC4_UF_A` and `PC4_UF_B`) are distinct pieces in the program's
model, as are the four X-centres on the same face (e.g., `PC4_U_FL`, `PC4_U_FR`,
`PC4_U_BL`, `PC4_U_BR`). A human cannot visually distinguish these when they occupy the
same physical slot, but the program does not need human-readable internal state.

Inner-slice-only effects (isolating just the inner layer, e.g. `u`) are **not primitive
moves** — they are achieved by composition within the IDA* search: `u = Uw U'`. The
IDA* therefore naturally finds algorithms that separate individual wings or centres
through sequences of outer and wide moves, without needing a third move type.

**`_io` is restricted to a corners-only algorithm** (corners are individually
distinguishable by their 3-color combination), so that the program output is
human-readable without any slot-counting convention.

### 7.3 Move table structure

```c
/* MOVE_TABLE4[face][q-1][depth-1], depth in {1,2} */
static uint8_t MOVE_TABLE4[FACE_COUNT][3][2][FACELET4_COUNT];
```

Total: 36 move tables (18 outer + 18 wide). IDA* uses all 36.

### 7.4 Authoring strategy

The 6 CW outer-face permutations will be written by hand as 96-entry `uint8_t` arrays,
following the same approach as `cube3.c`. The 6 CW wide-face permutations are also
written by hand (they extend the outer move by one inner layer). All q=2 and q=3
variants are derived by composition as in cube3.

### 7.3 Correctness assertion (debug build)

```c
/* Every move^4 = identity; opposite faces commute; each move sends
   every cubie to exactly one destination cubie (no half-permuted slots). */
```

The same three debug assertions used in `cube3.c` are replicated for the 96-slot world.

---

## 8. Algorithm Search for 4×4 (No Kociemba)

Kociemba's two-phase algorithm is specific to the 3×3 (its coordinate system is
hard-coded to 20 pieces). For the 4×4, we need a different solver.

### 8.1 Approach: IDA* with the 4×4 move set

The existing `regalloc.c` IDA* (capped at depth 7) already handles the 3×3 fallback
case. For the 4×4 we expose a standalone `cube4_idas_find(target, max_depth, out)` in
`cube4.c`. The move set is 36 moves (18 outer + 18 wide). Inner-slice effects are
reached implicitly through compositions such as `Uw U'` — no third primitive is needed.

This is enough for **register algorithm search** because:
- We only need to find *some* algorithm with the right cycle structure and order, not
  the globally shortest one.
- Small-order registers (orders 2–15) are almost always findable within depth 6–8.
- Higher-order registers on 4×4 will require deeper search or bidirectional BFS
  (see §8.2).

### 8.2 Future extension: bidirectional BFS

For high-order registers (order > ~30) a meet-in-the-middle BFS from identity and from
the target state is more practical. This can be added to `cube4.c` as
`cube4_bfs_find(target, out)` without touching any other layer. The CCS4 solver
(§10) will try IDA* first and fall back to BFS.

### 8.3 Pruning table

A simple lower-bound heuristic: count the number of pieces not in their home slot,
divided by the maximum pieces a single move touches. This gives a cheap, admissible
h-value that prunes IDA* significantly without requiring pre-built pruning tables.

---

## 9. CubeImpl Vtable (cube_impl.h)

`regalloc.c`, `ccs.c`, and `codegen.c` currently call cube3 functions directly.
To support both cube sizes without a maze of `#if` guards, we introduce a thin vtable:

```c
typedef struct CubeImpl {
    int  facelet_count;   /* 48 or 96 */
    int  piece_count;     /* 20 or 56 */
    int  max_order;       /* 1260 or ??? */

    void (*init)(void);

    /* State manipulation — void* to avoid having two separate CubeState types
       in every signature; callers cast from CubeState3* or CubeState4*. */
    void (*identity)(void *s);
    bool (*is_identity)(const void *s);
    void (*apply_sequence)(void *s, const Alg *a);
    bool (*equal)(const void *a, const void *b);

    /* Algorithm analysis */
    int      (*compute_order)(const Alg *a);
    uint64_t (*cycleset_from_alg)(const Alg *a);  /* CycleSet3 fits in uint64_t */
    bool     (*cyclesets_disjoint)(uint64_t a, uint64_t b);

    /* Algorithm search — find alg with given cycle structure, fill *out */
    bool (*find_alg)(const void *reg_spec, Alg *out);

    /* Printing */
    void (*cycleset_print)(uint64_t cs);
    int  state_size;      /* sizeof(CubeState3) or sizeof(CubeState4) */
} CubeImpl;

const CubeImpl *cube_impl_3x3(void);
const CubeImpl *cube_impl_4x4(void);
```

The 3×3 implementation wraps the existing cube3 functions. The `CycleSet` used
throughout is widened to `uint64_t` internally; the 3×3 wrapper just zero-extends.

All callers that currently use `CycleSet` (a `uint32_t`) will be changed to `uint64_t`.
This is a pure widening — no existing 3×3 behaviour changes.

---

## 10. CCF4 / CCS4 Adaptation

`ccf.c` is 3×3-specific: it hardcodes `CCF_CORNER_COUNT=8`, `CCF_EDGE_COUNT=12`,
orientation periods 3 and 3. For the 4×4 the orbits are different:

| Orbit        | Count | Pieces | Orientation period |
|--------------|-------|--------|--------------------|
| Corners      |     8 |      8 |                  3 |
| Wing edges   |    24 |     24 |                  2 |
| X-centres    |    24 |     24 |                  1 (no twist) |

### 10.1 New file: `src/cube/ccf4.h` / `ccf4.c`

Rather than parameterising `ccf.c`, we create a separate `ccf4.c` that repeats the
three-step pipeline with 4×4 constants. The interface is structurally identical:
`ccf4_run`, `ccf4_free`, `ccf4_select`, `ccf4_dump`. The `CCFArchitecture4` struct
replaces `CCFArchitecture` throughout the 4×4 path.

Achievable prime powers on the 4×4 (examples):
- Corners: same as 3×3 (up to 9 via corner cycle).
- Wings: up to order 24 from a full 12-cycle with a flip (period 2 × 12 = 24).
- X-centres: cycles up to length 24, no twist contribution → order = cycle length.
- Combinations via LCM can reach much higher orders than 1260.

### 10.2 CCS4

`ccs4.c` mirrors `ccs.c`: it enumerates piece assignments from the 4×4 piece pools
and calls `cube4_idas_find` (or `cube4_bfs_find`) in place of `kociemba_solve_state`.
The `CCSRegister4` struct carries a `CycleSet4` instead of `CycleSet`.

---

## 11. Regalloc / Codegen Changes

`regalloc.c` and `codegen.c` are the two files most dependent on the cube type.

### 11.1 regalloc.c

- Replace all `CycleSet` with `uint64_t`.
- `regalloc_init(table, impl)` — accepts a `const CubeImpl *` and stores it.
- `regalloc_find_and_add` calls `impl->find_alg` instead of `kociemba_solve_state`.
- `ALL_MOVES` table: for cube4, expand to 36 entries (18 outer + 18 wide); this
  stays inside `regalloc.c` and is selected based on `impl`.
- The `_io` register (R0) is still cube3-specific. When targeting cube4, a new
  default `_io` algorithm is chosen: a corner + wing-edge combination giving
  order ≥ 9. Exact algorithm TBD once cube4_init is working.

### 11.2 codegen.c

No cube-type-specific logic is needed in `codegen.c`. It operates on `RegTable`
entries (which carry the algorithm string and order); it never directly calls cube
functions. No changes required.

### 11.3 main.c

A `--cube4` flag (or `--cube-size=4`) selects the cube implementation:

```c
const CubeImpl *impl = use_cube4 ? cube_impl_4x4() : cube_impl_3x3();
```

`impl` is threaded into `regalloc_init`, `ccs4_solve`, and `ccf4_run`. Everything
else in the compiler pipeline (frontend, typechecker, desugarer, liveness,
interference) is completely cube-size-agnostic.

---

## 12. R0 / `_io` for cube4

On the 3×3, `_io` uses `B2 U D L2 B2 D2 R2 F2 U R U' R' D R U R` (order 9,
defined in `piece.h`). On the 4×4 the same algorithm is still a valid 4×4 sequence
(all moves are outer-face depth=1), so it is reusable as a starting point. However:

- Its order on the 4×4 may differ from 9 (it now also permutes inner layers).
- We must verify `cube4_compute_order(io_alg)` at startup, as is done on 3×3.
- If the order is unsuitable, a replacement is found via `cube4_idas_find`.

The `R0_ALGORITHM` macro in `piece.h` remains the 3×3 default. A separate
`R0_ALGORITHM_4X4` macro (or the runtime search) provides the 4×4 default.

---

## 13. Build System

### 13.1 New source files added to Makefile

```makefile
EXT_SRC += \
    src/cube/cube4.c \
    src/cube/ccf4.c \
    src/cube/ccs4.c
```

`cube4.c` is always compiled into the binary; the cube implementation is selected at
runtime via the vtable, not at compile time. This avoids separate binaries.

### 13.2 Test targets

```makefile
test-cube4: tests/test_cube4
	./tests/test_cube4

tests/test_cube4: tests/test_cube4.c src/cube/cube4.c src/util.c
	$(CC) $(CFLAGS) $(IFLAGS) $^ -o $@
```

The test file verifies:
1. Every outer face move has order 4.
2. Every wide move has order 4.
3. Opposite faces commute.
4. `cube4_cycleset_from_alg` returns the expected pieces for each single face move.
5. `cube4_compute_order` matches known orders for simple sequences.

---

## 14. Implementation Sequence

The following order minimises integration risk. Each step is independently buildable.

| Step | Deliverable | Depends on |
|------|-------------|------------|
| 1 | `piece4.h` — 56 piece labels | nothing |
| 2 | `cube4.h` — type declarations, no impl | step 1 |
| 3 | `cube4.c` — `cube4_init` + 6 outer CW tables (hand-written) | step 2 |
| 4 | `cube4.c` — compose q=2,3; build wide tables | step 3 |
| 5 | Debug assertions + `test_cube4` | step 4 |
| 6 | `cube4_compute_order` + `cube4_cycleset_from_alg` | step 5 |
| 7 | `cube4_idas_find` (IDA* solver) | step 6 |
| 8 | `cube_impl.h` + vtable wrappers for 3×3 and 4×4 | steps 2–7 |
| 9 | Widen `CycleSet` → `uint64_t` in `regalloc.c`, `ccs.c` | step 8 |
| 10 | `ccf4.c` / `ccs4.c` | steps 8–9 |
| 11 | `--cube4` flag in `main.c` | step 10 |
| 12 | 4×4 `_io` register selection | step 11 |
| 13 | `test_cube4` end-to-end: compile and run a `.cbyte` program | step 12 |

---

## 15. Known Constraints and Open Questions

**Algorithm search depth.** The 4×4 has a much larger state space than the 3×3.
IDA* at depth 7 may fail for many useful cycle structures. If that turns out to be
the case, the bidirectional BFS (§8.2) or a 4×4-specific pruning table is needed
before `ccf4`/`ccs4` can be practically useful.

**Maximum order.** The maximum order of a 4×4 permutation is not 1260. The exact
value needs to be computed (or sourced from the literature) to set `MAX_4X4_ORDER`
and guard `cube4_compute_order` against infinite loops.

**_io algorithm.** The 3×3 `_io` algorithm applied to the 4×4 will also move inner
layers (the `_B` wing edges). This may produce a different order and different cycle
set than expected. The exact behaviour must be checked after step 6.

**CycleSet widening.** Changing `CycleSet` from `uint32_t` to `uint64_t` is safe for
the 3×3 (all 20 pieces fit in 20 bits of either type) but it changes struct sizes and
binary cache formats. The existing `regalloc-registers.bin` cache is invalidated and
must be regenerated; the cache format version should be bumped.

**Parity.** The 4×4 has two distinct parity issues (OLL parity and PLL parity) not
present on the 3×3. These affect which states are reachable from identity with outer
face moves alone. Algorithm search must only target even-parity states, or the solver
must include inner slice moves in its move set. Using wide moves (depth=2) in the
IDA* move set naturally handles this since `Uw = U ∘ u`, where `u` is an inner slice
that introduces parity.

**CCF4 prime powers.** The achievable prime powers on the 4×4 orbit structure differ
significantly from the 3×3. `ccf4.c` will need a new `CCF4_PRIME_POWERS` table
derived from the 4×4 piece-budget analysis (56 pieces, 3 orbits with periods 3, 2, 1).
