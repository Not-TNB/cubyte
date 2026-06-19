#include "../include/alg.h"
#include "../include/cube.h"
#include "../include/util.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 64

/* ===========================================================================
 * MEMORY HELPERS
 *
 * Alg owns its m[] array.  alg_ensure_cap doubles capacity using the
 * standard doubling strategy (start at 8, double on overflow).
 * alg_push is the only way moves are appended; all other functions go
 * through it so the capacity logic stays in one place.
 * =========================================================================== */

void alg_free(Alg *a) {
    free(a->m);
    a->m   = NULL;
    a->len = a->cap = 0;
}

static void alg_ensure_cap(Alg *a, const int needed) {
    if (needed <= a->cap) return;
    int newcap = next_cap(a->cap);
    while (newcap < needed) newcap *= 2;
    Move *p = realloc(a->m, (size_t)newcap * sizeof(Move));
    if (!p) die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "OOM: alg_ensure_cap");
    a->m   = p;
    a->cap = newcap;
}

static void alg_push(Alg *a, const uint8_t face, const uint8_t q) {
    alg_ensure_cap(a, a->len + 1);
    Move mv;
    mv.face = face;
    mv.q    = q;
    a->m[a->len++] = mv;
}

/* ===========================================================================
 * alg_concat
 *
 * Algorithm: grow dst to hold src->len additional moves, then copy src's
 * move array into the newly allocated tail.  Single realloc + memcpy; O(n).
 * =========================================================================== */

void alg_concat(Alg *dst, const Alg *src) {
    if (src->len == 0) return;
    alg_ensure_cap(dst, dst->len + src->len);
    memcpy(dst->m + dst->len, src->m, (size_t)src->len * sizeof(Move));
    dst->len += src->len;
}

/* ===========================================================================
 * alg_to_string
 *
 * Algorithm: allocate worst-case 3*len+1 bytes (each token is at most two
 * chars — face letter + modifier — plus one space separator).  Walk the move
 * array once, writing face letter then optional modifier, space-separated.
 *
 * Face letters come from indexing into "UDLRFB" by the Face enum value so
 * the output is always uppercase regardless of how the Alg was parsed.
 *
 * q=1 → nothing appended   ("U"  = one CW quarter-turn)
 * q=2 → '2' appended       ("U2" = half turn)
 * q=3 → '\'' appended      ("U'" = CCW quarter-turn)
 * =========================================================================== */

char *alg_to_string(const Alg *a) {
    if (a->len == 0) {
        char *s = malloc(1);
        if (!s) die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "OOM: alg_to_string");
        s[0] = '\0';
        return s;
    }

    char *s = malloc((size_t)(3 * a->len + 1));
    if (!s) die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "OOM: alg_to_string");
    static const char face_chars[] = "UDLRFB";
    int pos = 0;

    for (int i = 0; i < a->len; i++) {
        if (i > 0) s[pos++] = ' ';
        s[pos++] = face_chars[a->m[i].face];
        if      (a->m[i].q == 2) s[pos++] = '2';
        else if (a->m[i].q == 3) s[pos++] = '\'';
    }
    s[pos] = '\0';
    return s;
}

/* ===========================================================================
 * alg_invert
 *
 * Algorithm: the inverse of "A B C …" is "… C' B' A'".  Walk in[] backwards,
 * flipping each q with (4 - q):
 *   q=1 (CW)  → q=3 (CCW)
 *   q=2 (180) → q=2 (180, self-inverse)
 *   q=3 (CCW) → q=1 (CW)
 *
 * out must be a fresh zero-initialised Alg (not previously freed here).
 * =========================================================================== */

void alg_invert(const Alg *in, Alg *out) {
    memset(out, 0, sizeof(*out));
    alg_ensure_cap(out, in->len);
    for (int i = in->len - 1; i >= 0; i--) {
        Move mv;
        mv.face    = in->m[i].face;
        mv.q       = (uint8_t)(4 - in->m[i].q);
        out->m[out->len++] = mv;
    }
}

/* ===========================================================================
 * alg_power_realise
 *
 * Algorithm: direction choice — use whichever direction requires fewer moves.
 *
 *   forward:  repeat A as-is,  m     times  (m   moves total per move in A)
 *   backward: repeat A^{-1},   K-m   times  (K-m moves total per move in A)
 *
 * We pick forward when m <= K-m (tie goes to forward).  The savings can be
 * dramatic: U^3 = 3 moves forward but only 1 move (U') backward.
 *
 * *out must be a fresh zero-initialised Alg.
 * =========================================================================== */
void alg_power_realise(const Alg *a, const int m, const int K, Alg *out) {
    memset(out, 0, sizeof(*out));
    if (m == 0) return;

    bool use_inv = (m > K - m);
    int  reps    = use_inv ? (K - m) : m;

    if (use_inv) {
        Alg inv;
        memset(&inv, 0, sizeof(inv));
        alg_invert(a, &inv);
        for (int i = 0; i < reps; i++) alg_concat(out, &inv);
        alg_free(&inv);
    } else {
        for (int i = 0; i < reps; i++) alg_concat(out, a);
    }
    alg_simplify(out);
}

/* ===========================================================================
 * alg_simplify  (spec Decision D5, Stage 2)
 *
 * Two rewrite rules applied to fixpoint:
 *
 *   Rule 1 — MERGE  (same face adjacent)
 *     new_q = (q1 + q2) % 4
 *     new_q == 0 → delete both   (e.g. U U' → ε)
 *     new_q != 0 → replace both  (e.g. U U → U2)
 *
 *   Rule 2 — SWAP  (opposite face pair in non-canonical order)
 *     Opposite pairs by enum value: {U=0,D=1}, {L=2,R=3}, {F=4,B=5}.
 *     Since each pair is consecutive, two faces are opposite iff face_a XOR
 *     face_b == 1.  Canonical order: lower enum value first (U before D, etc.).
 *     Swapping bubbles same-face moves together so Rule 1 can fire.
 *
 * Implementation: a write-cursor pass for Rule 1 (handles cascading merges in
 * one linear pass by always comparing against the last *written* move), then a
 * single sweep for Rule 2.  The outer loop re-runs both until neither changes
 * anything.
 *
 * Termination: each Merge strictly shrinks len; each Swap (without a Merge)
 * reduces the number of out-of-order adjacent opposite-face pairs without
 * increasing len.  The lexicographic measure (len, inversion-count) strictly
 * decreases on every rule firing.
 * =========================================================================== */

static bool merge_pass(Alg *a) {
    bool changed = false;
    int  w = 0;

    for (int r = 0; r < a->len; r++) {
        Move mv = a->m[r];
        if (w > 0 && a->m[w - 1].face == mv.face) {
            int new_q = (a->m[w - 1].q + mv.q) % 4;
            if (new_q == 0) {
                w--;                          /* both moves cancel */
            } else {
                a->m[w - 1].q = (uint8_t)new_q;   /* replace with combined */
            }
            changed = true;
        } else {
            a->m[w++] = mv;
        }
    }
    a->len = w;
    return changed;
}

static bool swap_pass(const Alg *a) {
    bool changed = false;
    for (int i = 0; i < a->len - 1; i++) {
        const int fa = a->m[i].face;
        const int fb = a->m[i + 1].face;
        // Opposite pair and out of canonical order → swap
        if ((fa ^ fb) == 1 && fa > fb) {
            const Move tmp    = a->m[i];
            a->m[i]     = a->m[i + 1];
            a->m[i + 1] = tmp;
            changed = true;
        }
    }
    return changed;
}

void alg_simplify(Alg *a) {
    bool changed = true;
    while (changed) {
        changed  = merge_pass(a);
        changed |= swap_pass(a);
    }
}

/* ===========================================================================
 * alg_parse
 *
 * Parses a SiGN-notation algorithm string (including groups and wide moves)
 * into an Alg.  Returns true and populates *out on success; returns false and
 * leaves *out unchanged on any error.
 *
 * --- Token grammar ---
 *
 *   token  ::=  '('
 *            |  ')' digit*          e.g. ')', ')3', ')12'
 *            |  letter modifier?
 *
 *   letter   ::= U D L R F B
 *   modifier ::= "'" | '2'
 *
 * Whitespace between tokens is ignored.
 *
 * --- Move token semantics ---
 *
 *   digit-prefix  width (default 1; parsed for validation, discarded)
 *   letter        face (uppercase only)
 *   'w'           marks move as wide (width discarded in CuBit's model)
 *   modifier      q: nothing→1, '2'→2, "'"→3
 *
 *   Width ≥ 2 means a "wide move".  CuBit accepts these for oracle
 *   compatibility but discards the width, storing only face and q.
 *
 * --- Group handling ---
 *
 *   A stack of Alg accumulators handles nested parentheses:
 *     - stk[0]   = base accumulator (not from a '(')
 *     - '('      pushes a new empty accumulator (is_open=true)
 *     - ')N'     pops the top, concats it N times into the new top
 *
 *   Unlike the Python reference (which built a flat stack of Move objects and
 *   popped them LIFO, requiring a reversal), this C implementation builds each
 *   level's Alg in forward order, so no reversal is needed when closing a group.
 *
 * --- Error conditions ---
 *
 *   - Invalid character in token stream
 *   - Unrecognised face letter (not in UDLRFB)
 *   - ')' with no matching '('
 *   - '(' with no matching ')'
 *   - Multiplier ≤ 0 after ')'
 *
 *   On any error all partially-built stack entries are freed; *out is
 *   untouched. alg_parse never calls die().
 * =========================================================================== */

static int face_from_char(const char c) {
    switch (c) {
        case 'U': return FACE_U;
        case 'D': return FACE_D;
        case 'L': return FACE_L;
        case 'R': return FACE_R;
        case 'F': return FACE_F;
        case 'B': return FACE_B;
        default:  return -1;
    }
}

/*
 * parse_move_token: decode one scanned move token (e.g. R, U', F2) into a Move.
 */
static bool parse_move_token(const char *tok, Move *out) {
    const char *p = tok;

    // face letter
    const char fc   = *p++;
    const int  face = face_from_char(fc);
    if (face < 0) return false;

    // optional modifier
    int q = 1;
    if      (*p == '\'') { q = 3; p++; }
    else if (*p == '2')  { q = 2; p++; }

    if (*p != '\0') return false;

    out->face = (uint8_t)face;
    out->q    = (uint8_t)q;
    return true;
}

/*
 * next_token: advance *pp past any leading whitespace and scan the next token
 * into tokbuf.
 *
 * Returns: 1 = token written, 0 = end of string, -1 = scan error.
 */
#define PUSH(c) do { if (i >= bufsz - 1) return -1; tokbuf[i++] = (c); } while(0)
static int next_token(const char **pp, char *tokbuf, int bufsz) {
    const char *p = *pp;

    // Skip whitespace
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '\0') { *pp = p; return 0; }

    int i = 0;

    if (*p == '(') {
        PUSH(*p++);
    } else if (*p == ')') {
        PUSH(*p++);
        while (*p >= '0' && *p <= '9') PUSH(*p++);
    } else {
        // Move token: digits*, letter, 'w'?, modifier?
        while (*p >= '0' && *p <= '9') PUSH(*p++);
        if (*p >= 'A' && *p <= 'Z') {
            PUSH(*p++);
        } else {
            return -1; // expected letter
        }
        if (*p == 'w')               PUSH(*p++);
        if (*p == '\'' || *p == '2') PUSH(*p++);
    }

    tokbuf[i] = '\0';
    *pp = p;
    return 1;
}
#undef PUSH

bool alg_parse(const char *text, Alg *out) {
    Alg  stk[MAX_DEPTH];
    bool is_open[MAX_DEPTH]; // true iff this level was pushed by '('
    int  depth = 0;

    // Push base accumulator (not from a '(')
    memset(&stk[0], 0, sizeof(stk[0]));
    is_open[0] = false;
    depth      = 1;

    const char *p  = text;
    char        tok[64];
    bool        ok = true;

    while (ok) {
        int r = next_token(&p, tok, sizeof tok);
        if (r ==  0) break;    // end of input
        if (r  < 0) { ok = false; break; }

        if (tok[0] == '(') {
            if (depth >= MAX_DEPTH) { ok = false; break; }
            memset(&stk[depth], 0, sizeof(stk[depth]));
            is_open[depth] = true;
            depth++;

        } else if (tok[0] == ')') {
            // Unmatched ')'
            if (depth < 2 || !is_open[depth - 1]) { ok = false; break; }

            // Parse multiplier (digits after ')'; absent → 1)
            int mul = 1;
            if (tok[1] != '\0') {
                mul = atoi(tok + 1);
                if (mul <= 0) { ok = false; break; }
            }

            // Pop inner alg and concat mul times into the new top of stack
            Alg inner = stk[--depth];
            for (int i = 0; i < mul; i++) alg_concat(&stk[depth - 1], &inner);

            alg_free(&inner);
        } else {
            // Regular move token
            Move mv;
            if (!parse_move_token(tok, &mv)) { ok = false; break; }
            alg_push(&stk[depth - 1], mv.face, mv.q);
        }
    }

    if (ok && depth != 1) ok = false; // unmatched '('

    if (ok) {
        *out = stk[0];
        return true;
    }

    // fail
    for (int i = 0; i < depth; i++) alg_free(&stk[i]);
    return false;
}
