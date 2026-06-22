#include "../include/ccf.h"
#include "../include/cube.h"
#include "../include/alg.h"
#include "../include/piece.h"
#include "../include/ccs.h"
#include "../include/kociemba.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

// Stage 1 — piece→facelet table (orientation frame), derived geometrically.
//
// PIECE_FACELETS[p] lists piece p's facelet slots with a CONSISTENT orientation
// frame so that the k->k sticker mapping used by build_target corresponds to a
// twist-0 / flip-0 cube motion. Slot [0] is the "primary" sticker (U or D if the
// piece has one, else F or B, else L or R). For corners, the remaining two slots
// are ordered by a fixed handedness (positive scalar triple product of the face
// normals), which is invariant under cube rotations — so two corners related by a
// rotation get matching frames, and cycling them with k->k introduces no twist.
//
// A hand-written table is error-prone here (the original mislabelled the
// handedness of half the corners, making every corner cycle unreachable), so we
// derive the frame from cube.c's verified facelet_to_piece table at first use.
static uint8_t PIECE_FACELETS[PC_COUNT][3];
static bool pf_ready = false;

/* Priority for the primary sticker: U/D (0) > F/B (1) > L/R (2). */
static int face_primary_rank(int face) {
    if (face == 0 || face == 1) return 0; /* U/D */
    if (face == 4 || face == 5) return 1; /* F/B */
    return 2;                             /* L/R */
}

/* Trace one cubie through every position reachable by face moves, recording a
 * consistent orientation-0 frame at each. Because all visited frames belong to
 * the SAME physical cubie, the k-th entry is always the same physical sticker,
 * so cycling pieces with the k->k mapping in build_target adds no net twist/flip
 * — the produced state is reachable and has order equal to the cycle length. */
static void trace_orbit(const uint8_t start[3], int nf,
                        const uint8_t fmap[18][FACELET_COUNT], bool seen[PC_COUNT]) {
    uint8_t queue[12][3];
    int head = 0, tail = 0;

    int p0 = facelet_to_piece[start[0]];
    seen[p0] = true;
    for (int k = 0; k < 3; k++) PIECE_FACELETS[p0][k] = (k < nf) ? start[k] : 0xFF;
    memcpy(queue[tail++], start, 3);

    while (head < tail) {
        uint8_t cur[3];
        memcpy(cur, queue[head++], 3);
        for (int m = 0; m < 18; m++) {
            uint8_t nxt[3] = {0xFF, 0xFF, 0xFF};
            for (int k = 0; k < nf; k++) nxt[k] = fmap[m][cur[k]];
            if (getenv("PFDBG") && nf == 2 && facelet_to_piece[nxt[0]] != facelet_to_piece[nxt[1]])
                fprintf(stderr, "[trace] move %d: cur=[%d,%d] -> nxt=[%d,%d] NOT co-located (pieces %d,%d)\n",
                        m, cur[0], cur[1], nxt[0], nxt[1],
                        facelet_to_piece[nxt[0]], facelet_to_piece[nxt[1]]);
            int pos = facelet_to_piece[nxt[0]];
            if (seen[pos]) continue;
            seen[pos] = true;
            for (int k = 0; k < 3; k++) PIECE_FACELETS[pos][k] = nxt[k];
            memcpy(queue[tail++], nxt, 3);
        }
    }
}

/* Outward unit normal of a face index (U,D,L,R,F,B), as integer xyz. */
static void face_normal(int face, int v[3]) {
    static const int N[6][3] = {
        {0,1,0}, {0,-1,0}, {-1,0,0}, {1,0,0}, {0,0,1}, {0,0,-1}
    };
    v[0]=N[face][0]; v[1]=N[face][1]; v[2]=N[face][2];
}

/* Corner orientation frame, derived geometrically. Corners DO have a
 * rotation-invariant orientation coordinate: order the three stickers with the
 * U/D sticker first and the remaining two in a fixed handedness (positive scalar
 * triple product of face normals). Because handedness is preserved by cube
 * rotations, the k->k mapping is the standard orientation-0 placement, so an
 * oriented cycle realizes exactly its intended net twist.  (Move-tracing does
 * NOT give this — it is consistent for pure permutation but injects spurious
 * twist into oriented corner cycles.) */
static void build_corner_frame(int p, const int slots[3]) {
    int prim = 0;
    for (int i = 1; i < 3; i++)
        if (face_primary_rank(slots[i] / 8) < face_primary_rank(slots[prim] / 8)) prim = i;
    int ord[3]; ord[0] = slots[prim];
    int j = 1;
    for (int i = 0; i < 3; i++) if (i != prim) ord[j++] = slots[i];

    int n0[3], n1[3], n2[3];
    face_normal(ord[0]/8, n0); face_normal(ord[1]/8, n1); face_normal(ord[2]/8, n2);
    int cx = n0[1]*n1[2]-n0[2]*n1[1], cy = n0[2]*n1[0]-n0[0]*n1[2], cz = n0[0]*n1[1]-n0[1]*n1[0];
    int trip = cx*n2[0]+cy*n2[1]+cz*n2[2];
    /* Kociemba's cornerFacelet defines secondary sticker order as: swap the
     * two secondary slots when the scalar triple product of face normals is
     * non-negative.  This holds for both U-primary and D-primary corners —
     * U-primary have trip > 0 (need swap), D-primary have trip < 0 (no swap),
     * and Kociemba's table agrees in both cases.  The previous formula
     * (swap when trip<0 then flip for D-primary) only fixed D-primary and
     * broke cross-layer corner cycles where the U→D handedness mismatch
     * caused Kociemba to misidentify pieces (return -4, "Unsolvable cube!"). */
    bool want_swap = (trip >= 0);
    if (want_swap) { int t = ord[1]; ord[1] = ord[2]; ord[2] = t; }

    for (int k = 0; k < 3; k++) PIECE_FACELETS[p][k] = (uint8_t)ord[k];
}

static void ensure_piece_facelets(void) {
    if (pf_ready) return;

    /* Corners: geometric (rotation-invariant) orientation frame. */
    for (int p = 0; p < PC_COUNT; p++) {
        int slots[3], nf = 0;
        for (int s = 0; s < FACELET_COUNT && nf < 3; s++)
            if (facelet_to_piece[s] == (PieceLabel)p) slots[nf++] = s;
        if (nf == 3) build_corner_frame(p, slots);
    }

    /* Edges have no rotation-invariant handedness, so derive their flip frame by
     * tracing one real edge through the verified moves (consistent for pure and
     * oriented edge cycles alike). Build forward facelet maps first. */
    uint8_t fmap[18][FACELET_COUNT];
    int mi = 0;
    for (int f = 0; f < FACE_COUNT; f++) {
        for (int q = 1; q <= 3; q++) {
            CubeState s;
            for (int i = 0; i < FACELET_COUNT; i++) s.state[i] = (uint8_t)i;
            cube_apply_move(&s, (Face)f, q);
            for (int j = 0; j < FACELET_COUNT; j++)
                for (int i = 0; i < FACELET_COUNT; i++)
                    if (s.state[i] == j) { fmap[mi][j] = (uint8_t)i; break; }
            mi++;
        }
    }

    bool seen[PC_COUNT] = {false};
    for (int p = 0; p < PC_COUNT; p++) {
        int slots[3], nf = 0;
        for (int s = 0; s < FACELET_COUNT && nf < 3; s++)
            if (facelet_to_piece[s] == (PieceLabel)p) slots[nf++] = s;
        if (nf != 2) continue; /* edges only */
        int prim = (face_primary_rank(slots[1]/8) < face_primary_rank(slots[0]/8)) ? 1 : 0;
        uint8_t start[3] = {(uint8_t)slots[prim], (uint8_t)slots[1-prim], 0xFF};
        trace_orbit(start, 2, fmap, seen);
        break; /* one edge start covers the whole edge orbit */
    }
    pf_ready = true;

    if (getenv("PFDBG")) {
        int seen_slot[FACELET_COUNT] = {0};
        for (int p = 0; p < PC_COUNT; p++) {
            fprintf(stderr, "[pf] piece %2d:", p);
            for (int k = 0; k < 3; k++) {
                if (PIECE_FACELETS[p][k] == 0xFF) continue;
                fprintf(stderr, " %d", PIECE_FACELETS[p][k]); seen_slot[PIECE_FACELETS[p][k]]++;
            }
            fprintf(stderr, "\n");
        }
        for (int s = 0; s < FACELET_COUNT; s++)
            if (seen_slot[s] != 1) fprintf(stderr, "[pf] SLOT %d used %d times!\n", s, seen_slot[s]);
    }
}

static inline int piece_facelet_count(PieceLabel p) {
    return (PIECE_FACELETS[p][2] == 0xFF) ? 2 : 3;
}

// Stage 2 — build_target
// Given a fully-specified CCSRegister, write the permutation into a CubeState. Start from identity,
// then for each cycle step i: src's k-th slot's value goes into dest's k-th slot
// (ori_delta is zero, so no offset). One loop over cycles, one loop over
// pieces within the cycle, one loop over facelets.
static void build_target(CubeState *T, const CCSRegister *reg) {
    ensure_piece_facelets();
    cube_identity(T);
    for (int c=0; c<reg->abstract.num_cycles; c++) {
        const CCSCycle *cy = &reg->cycles[c];
        int n = cy->abstract.length;
        int f = piece_facelet_count(cy->pieces[0]);

        for (int i=0; i<n; i++) {
            PieceLabel src = cy->pieces[i];
            PieceLabel dst = cy->pieces[(i+1)%n];
            int d = cy->ori_deltas[i];
            for (int k=0; k<f; k++) {
                int new = ((k + d) % f + f) % f;
                T->state[PIECE_FACELETS[dst][new]] = PIECE_FACELETS[src][k];
            }
        }
    }
}

// Stage 6 — ccs_find_alg
// The public step-level function. Builds the target state for the register's
// "add 1" operation, then asks Kociemba's two-phase solver for a face-turn
// sequence that produces it. Kociemba handles any reachable state in <= ~20
// moves, so there is no search-depth ceiling (unlike the old bidirectional
// BFS). On success, verifies the solved algorithm has the intended order.

bool ccs_find_alg(const CCSRegister *reg, Alg *out) {
    /* A register is applied on its own, so its permutation must be an
     * independently reachable cube state. Three cube laws must hold (cheap
     * pre-filters; Kociemba would otherwise reject the facelets as unsolvable):
     *   1. permutation parity even  (corner-perm parity == edge-perm parity)
     *   2. net corner twist  == 0 (mod 3)
     *   3. net edge flip     == 0 (mod 2)
     * net_orientation is stored per cycle; for a length-1 cycle it encodes a
     * pure in-place twist/flip. */
    int parity = 0, corner_ori = 0, edge_ori = 0;
    for (int c = 0; c < reg->abstract.num_cycles; c++) {
        const CCFCycle *cy = &reg->abstract.cycles[c];
        parity ^= (cy->length - 1) & 1;
        if (cy->orbit == 0) corner_ori += cy->net_orientation;
        else                edge_ori   += cy->net_orientation;
    }
    if (parity) return false;
    if (((corner_ori % 3) + 3) % 3 != 0) return false;
    if (((edge_ori   % 2) + 2) % 2 != 0) return false;

    CubeState T;
    build_target(&T, reg);

    if (getenv("CCSDBG") && reg->abstract.order == 6) {
        fprintf(stderr, "[dbg] order6 corner positions (pos<-piece, twist):");
        for (int p = 0; p < PC_COUNT; p++) {
            if (piece_facelet_count(p) != 3) continue;
            int s0 = PIECE_FACELETS[p][0], src = T.state[s0], q = facelet_to_piece[src], j = 0;
            for (int k = 0; k < 3; k++) if (PIECE_FACELETS[q][k] == src) { j = k; break; }
            if (q != p) fprintf(stderr, " %d<-%d(t%d)", p, q, j);
        }
        fprintf(stderr, "\n");
    }

    Alg alg = {0};
    if (!kociemba_solve_state(&T, &alg)) return false;

    /* Reject solutions whose order doesn't match the register's intended order
     * (guards against a malformed target or solver quirk). */
    if (compute_order(&alg) != reg->abstract.order) {
        fprintf(stderr,"[dbg] order got=%d want=%d\n", compute_order(&alg), reg->abstract.order);
        alg_free(&alg);
        return false;
    }

    *out = alg;
    return true;
}

// Stage 7 — Piece assignment enumeration
// ccs_enumerate_assignments: recursive choose_for_cycle (pick length pieces from pool in ascending index order,
// mark used[], recurse on next cycle, unmark). Two independent pools:
// CORNERS[8] and EDGES[12]. Callback fills pieces[]; abstract is copied in from the arch.

static const PieceLabel CORNERS[CCF_CORNER_COUNT] = {
    PC_UFL, PC_UFR, PC_UBL, PC_UBR, PC_DFL, PC_DFR, PC_DBL, PC_DBR
};

static const PieceLabel EDGES[CCF_EDGE_COUNT] = {
    PC_UF, PC_UL, PC_UR, PC_UB,
    PC_DF, PC_DL, PC_DR, PC_DB,
    PC_FL, PC_FR, PC_BL, PC_BR
};

typedef struct {
    const CCFArchitecture *arch;
    CCSCycle cycles[CCF_MAX_REGISTERS][CCF_MAX_CYCLES_PER_REG];
    bool corner_used[CCF_CORNER_COUNT];
    bool edge_used[CCF_EDGE_COUNT];
    CCSAssignCB cb;
    void *cb_ctx;
    int count;
    bool stop; /* set when cb returns false; unwinds the recursion */
} EnumCtx;

static void choose_for_cycle(EnumCtx *e, int reg, int cyc, int start, int filled);

static void choose_oris(EnumCtx *e, int reg, int cyc, int pos, int sum_so_far) {
    if (e->stop) return;
    const CCFCycle *cy = &e->arch->registers[reg].cycles[cyc];
    int n = cy->length;
    int f = (cy->orbit == 0) ? 3 : 2;
    int no = (cy->net_orientation % f + f) % f;

    if (no == 0) {
        /* Pure perm cycle */
        memset(e->cycles[reg][cyc].ori_deltas, 0, n * sizeof(int8_t));
        choose_for_cycle(e, reg, cyc+1, 0, 0);
        return;
    }

    if (pos == n-1) {
        int last = ((no - sum_so_far) % f + f) % f;
        e->cycles[reg][cyc].ori_deltas[pos] = (int8_t)last;
        choose_for_cycle(e, reg, cyc+1, 0, 0);
        return;
    }

    for (int d=0; d<f; d++) {
        e->cycles[reg][cyc].ori_deltas[pos] = (int8_t)d;
        choose_oris(e, reg, cyc, pos+1, sum_so_far+d);
        if (e->stop) return;
    }
}

static void choose_for_cycle(EnumCtx *e, int reg, int cyc, int start, int filled) {
    if (e->stop) return;
    const CCFArchitecture *arch = e->arch;

    if (reg == arch->num_registers) {
        e->count++;
        if (!e->cb(e->cycles, arch->num_registers, e->cb_ctx)) e->stop = true;
        return;
    }

    const CCFRegister *r = &arch->registers[reg];
    if (cyc == r->num_cycles) {
        choose_for_cycle(e, reg+1, 0, 0, 0);
        return;
    }

    const CCFCycle *cy = &r->cycles[cyc];
    int length = cy->length;
    int needed = length - filled;

    const PieceLabel *pool;
    int pool_size;
    bool *used;
    if (cy->orbit == 0) {
        pool = CORNERS;
        pool_size = CCF_CORNER_COUNT;
        used = e->corner_used;
    } else {
        pool = EDGES;
        pool_size = CCF_EDGE_COUNT;
        used = e->edge_used;
    }

    if (filled == 0) e->cycles[reg][cyc].abstract = *cy;

    for (int i=start; i<=pool_size-needed; i++) {
        if (used[i]) continue;
        used[i] = true;
        e->cycles[reg][cyc].pieces[filled] = pool[i];

        if (filled+1 == length) choose_oris(e, reg, cyc, 0, 0);
        else choose_for_cycle(e, reg, cyc, i+1, filled+1);

        used[i] = false;
        if (e->stop) return;
    }
}

int ccs_enumerate_assignments(const CCFArchitecture *arch,
                              CycleSet forbidden,
                              CCSAssignCB cb, void *ctx) {
    EnumCtx e = {0};
    e.arch = arch;
    e.cb = cb;
    e.cb_ctx = ctx;
    /* Pre-mark forbidden pieces so choose_for_cycle skips them naturally. */
    for (int i = 0; i < CCF_CORNER_COUNT; i++)
        if (forbidden & (CycleSet)(1u << (unsigned)CORNERS[i])) e.corner_used[i] = true;
    for (int i = 0; i < CCF_EDGE_COUNT; i++)
        if (forbidden & (CycleSet)(1u << (unsigned)EDGES[i])) e.edge_used[i] = true;
    choose_for_cycle(&e, 0, 0, 0, 0);
    return e.count;
}

// Stage 8 — ccs_solve
// The outer driver. Sets up a SolveCtx containing the arch and the result list,
// passes it as the callback context to ccs_enumerate_assignments. In the callback: zero-fill all
// ori_deltas, then for each register call ccs_find_alg. If every register succeeds,
// compute cycleset for each register, compute free_pieces, append the CCSArchitecture to the
// result. If any register fails, free the algorithms accumulated so far and return.

/* Cap on piece assignments attempted per architecture. All assignments of a
 * given orbit/length are equivalent under cube symmetry, so a valid CCF
 * architecture is solved by the first representative; the cap only bounds work
 * in pathological cases where every attempt fails (e.g. an unreachable order). */
#define CCS_MAX_ASSIGNMENT_ATTEMPTS 64

typedef struct {
    const CCFArchitecture *arch;
    CCSResult *out;
    int attempts;
} SolveCtx;

/* Returns true to keep enumerating, false to stop (success, or attempt cap). */
static bool solve_cb(CCSCycle cycles[CCF_MAX_REGISTERS][CCF_MAX_CYCLES_PER_REG], int num_regs, void *ctx_) {
    SolveCtx *sctx = ctx_;
    const CCFArchitecture *arch = sctx->arch;
    CCSResult *out = sctx->out;
    sctx->attempts++;

    CCSArchitecture carch;
    carch.abstract = arch;
    carch.num_regs = num_regs;

    for (int r=0; r<num_regs; r++) {
        CCSRegister *reg = &carch.regs[r];
        reg->abstract = arch->registers[r];

        int nc = arch->registers[r].num_cycles;
        for (int c=0; c<nc; c++)
            reg->cycles[c] = cycles[r][c];

        Alg alg = {0};
        if (!ccs_find_alg(reg, &alg)) {
            for (int rr=0; rr<r; rr++) alg_free(&carch.regs[rr].alg);
            /* This assignment failed; try another unless we've hit the cap. */
            return sctx->attempts < CCS_MAX_ASSIGNMENT_ATTEMPTS;
        }
        reg->alg = alg;
    }

    /* all registers solved — compute cyclesets and free_pieces */
    CycleSet used = CYCLESET_EMPTY;
    for (int r=0; r<num_regs; r++) {
        carch.regs[r].cycleset = cycleset_from_alg(&carch.regs[r].alg);
        used = cycleset_union(used, carch.regs[r].cycleset);
    }
    carch.free_pieces = CYCLESET_FULL & ~used;

    /* grow result array and append */
    if (out->count == out->cap) {
        int new_cap = out->cap ? out->cap * 2 : 8;
        out->archs = realloc(out->archs, new_cap * sizeof(CCSArchitecture));
        out->cap = new_cap;
    }
    out->archs[out->count++] = carch;

    /* One representative assignment is enough — stop enumerating. */
    return false;
}

void ccs_solve(const CCFArchitecture *arch, CCSResult *out) {
    SolveCtx sctx = {arch, out, 0};
    ccs_enumerate_assignments(arch, CYCLESET_EMPTY, solve_cb, &sctx);
}

// Stage 9 — Result management and public API
// ccs_free (free alg.m for each register in each arch, free archs),
// ccs_best (linear scan, pick minimum total alg.len),
// ccs_verify (four checks: order, cycleset, disjoint, free complement),
// ccs_dump (human-readable print).

void ccs_free(CCSResult *result) {
    for (int i=0; i<result->count; i++) {
        CCSArchitecture *arch = &result->archs[i];
        for (int r=0; r<arch->num_regs; r++) alg_free(&arch->regs[r].alg);
    }
    free(result->archs);
    result->archs = NULL;
    result->count = 0;
    result->cap = 0;
}

const CCSArchitecture *ccs_best(const CCSResult *result) {
    if (result->count == 0) return NULL;
    const CCSArchitecture *best = &result->archs[0];
    int best_len = 0;
    for (int r=0; r<best->num_regs; r++) best_len += best->regs[r].alg.len;

    for (int i=1; i<result->count; i++) {
        const CCSArchitecture *a = &result->archs[i];
        int len = 0;
        for (int r=0; r<a->num_regs; r++) len += a->regs[r].alg.len;
        if (len < best_len) {
            best = a;
            best_len = len;
        }
    }
    return best;
}

bool ccs_verify(const CCSArchitecture *arch) {
    CycleSet used = CYCLESET_EMPTY;

    for (int r=0; r<arch->num_regs; r++) {
        const CCSRegister *reg = &arch->regs[r];
        if (compute_order(&reg->alg) != reg->abstract.order) { fprintf(stderr,"[vf] reg%d order\n",r); return false; }
        CycleSet cs = cycleset_from_alg(&reg->alg);
        if (cs != reg->cycleset) { fprintf(stderr,"[vf] reg%d cycleset got=%x want=%x\n",r,cs,reg->cycleset); return false; }
        if (!cycleset_disjoint(cs, used)) { fprintf(stderr,"[vf] reg%d not disjoint cs=%x used=%x\n",r,cs,used); return false; }
        used = cycleset_union(used, cs);
    }

    if (arch->free_pieces != (CYCLESET_FULL & ~used)) { fprintf(stderr,"[vf] free got=%x want=%x\n",arch->free_pieces,CYCLESET_FULL&~used); return false; }
    return true;
}

void ccs_dump(const CCSArchitecture *arch, FILE *fp) {
    int total = 0;
    for (int r=0; r<arch->num_regs; r++) total += arch->regs[r].alg.len;
    fprintf(fp, "CCSArchitecture: %d reg(s), %d total moves\n", arch->num_regs, total);

    for (int r=0; r<arch->num_regs; r++) {
        const CCSRegister *reg = &arch->regs[r];
        char *s = alg_to_string(&reg->alg);
        fprintf(fp, "  reg[%d] order=%-4d  %s\n", r, reg->abstract.order, s);
        free(s);

        for (int c=0; c<reg->abstract.num_cycles; c++) {
            const CCSCycle *cy = &reg->cycles[c];
            fprintf(fp, "    cycle[%d] %s len=%d:",
                c, cy->abstract.orbit == 0 ? "corner" : "edge", cy->abstract.length);
            for (int i=0; i<cy->abstract.length; i++) fprintf(fp, " %s", piece_to_string(cy->pieces[i]));
            fprintf(fp, "\n");
        }
    }

    fprintf(fp, "  free: ");
    cycleset_print(arch->free_pieces);
    fprintf(fp, "\n");
}
