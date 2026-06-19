# CuBit System — Detailed Implementation Specification

*A task-by-task build guide for the CuBit compiler and emulator*

**CuBit Team**
**Version 1.2 — June 9, 2026**

---

## Table of Contents

1. [How to Use This Document](#1-how-to-use-this-document)
2. [Phase 0 — Shared Contracts (All Four, Day 1)](#2-phase-0--shared-contracts-all-four-day-1)
3. [Design Decisions: Register Operations and Move Shortening](#3-design-decisions-register-operations-and-move-shortening)
4. [Pipeline Order (main.c)](#4-pipeline-order-mainc)
5. [Person 1 — Language Frontend (lexer, AST, parser)](#5-person-1--language-frontend-lexer-ast-parser)
6. [Person 2 — Typechecker, Liveness, Interference Graph](#6-person-2--typechecker-liveness-interference-graph)
7. [Person 3 — Cube Model, Algorithm Engine, Register Allocator, IDA*](#7-person-3--cube-model-algorithm-engine-register-allocator-ida)
8. [Person 4 — Desugaring, Code Generator, Browser Emulator](#8-person-4--desugaring-code-generator-browser-emulator)
9. [Integration Plan and Milestones](#9-integration-plan-and-milestones)
10. [Report and Presentation Hooks](#10-report-and-presentation-hooks)

---

## 1. How to Use This Document

This document refines the CuBit Technical Specification (v1.0) into an ordered sequence of concrete implementation tasks, and fixes the design decisions that v1.0 left open — in particular the exact compilation and execution semantics of register assignment (`Ri := Rj`), add-immediate (`Ri := Ri + n`), constant re-assignment, and the move-shortening (algorithm simplification) pipeline. Those decisions live in Section 3 and are binding on all four members.

Each task states:

- **What to build** (files, functions, data structures).
- **Depends on** — what must exist (from you or a teammate) before you start.
- **Definition of done** — an objective completion criterion.
- **Pitfalls** — known traps, ambiguities, and design decisions resolved in advance.
- **Tests** — the minimum test artefacts you must produce.
- **Report material** — where the task feeds the assessed report.

Tasks within each person's section are ordered so that each one is testable in isolation before the next begins. Where a task touches a shared contract (Section 2), the contract is frozen first and must not change without a group vote and a commit to `contracts.h`.

> **Golden rule.** Every module must compile and pass its own unit tests without any other person's module present. Code against header files only, and write small `main()`-based test drivers under `tests/`.

> **Language policy (coursework rule).** The extension's central implementation must be C. Our existing Python algorithm parser is therefore not shipped as part of the compiler or emulator; it is retained, unchanged, as a test oracle (Section 2.9): the C cube engine and the JS port are both cross-validated against it in the test suite. Python in the test harness is explicitly permitted ("additional languages as helpers").

---

## 2. Phase 0 — Shared Contracts (All Four, Day 1)

Before anyone writes implementation code, the group creates a single header `src/contracts.h` (plus `ast.h`, owned by Person 1) containing everything two or more modules rely on. This phase should take one sitting; Section 3 is read aloud and signed off in the same sitting.

### 2.1 Task 0.1 — Freeze the canonical piece-label set

Define the `PieceLabel` enum (20 values, `PC_UFL` ... `PC_BR`, plus `PC_COUNT`) and a constant table mapping enum values to canonical strings (`"UFL"`, `"UF"`, ...). Provide two helpers, declared here, implemented in `util.c`:

```c
int piece_from_string(const char *s); /* -1 if invalid */
const char *piece_to_string(int p);
```

**Definition of done.** All four members convert between strings and enum values via one shared function; the typechecker (P2), regalloc (P3) and emulator (P4) all consume this table, never their own copies.

### 2.2 Task 0.2 — Freeze the R0 contract

```c
#define R0_ALGORITHM "U"
#define R0_ORDER 4
/* cycle set: UF, UFL, UR, UFR, UB, UBR, UL, UBL */
```

Convention: inside the compiler the I/O register is the synthetic variable `"R0"`; the typechecker predeclares it as `int`; liveness treats `output` as a use and `input` as a def of it.

**Definition of done.** The same three lines appear in `contracts.h` and are mirrored verbatim as constants in `index.html`.

### 2.3 Task 0.3 — Freeze error exit codes and message formats

Exit codes 0–5 exactly as in v1.0 §10.4. Every fatal error prints one line to `stderr` of the form `[stage] line N: message` (stages: `lexer`, `parse`, `typecheck`, `regalloc`, `internal`) and exits with the matching code. Provide in `util.h`:

```c
noreturn void die(int exit_code, const char *stage,
                  int line, const char *fmt, ...);
```

**Definition of done.** All members use `die()`; no module calls `exit()` directly.

### 2.4 Task 0.4 — Freeze the assembly text format

One instruction per line: line number, two spaces, instruction text. Line numbers strictly increasing in steps of 10. Labels are emitted as their own numbered lines ending in `:`. Branch piece lists are comma-separated with no spaces. P4 owns an EBNF of this format (Task 4.7), written before either the C emitter or the JS parser.

### 2.5 Task 0.5 — Reserved identifier rules

Two rules the lexer (P1) and typechecker (P2) both enforce:

1. User identifiers may not begin with `__` (reserved for compiler-generated labels and temporaries).
2. **Alg-variable naming restriction**: a variable of type `alg` may not be named `U`, `D`, `L`, `R`, `F` or `B`. Rationale: inside an alg expression the lexer is in alg-context and would tokenize these as moves, making such variables unreferenceable. Enforced at declaration time by the typechecker (Task 2.4).

### 2.6 Task 0.6 — Repository layout and Makefile skeleton

```
extension/
  src/ contracts.h util.[ch] lexer.[ch] parser.[ch] ast.h
       typechecker.[ch] liveness.[ch] regalloc.[ch]
       codegen.[ch] cube.[ch] alg.[ch] main.c
  emulator/ index.html TESTING.md
  tools/ algcheck.py (existing Python parser, oracle)
  tests/ lexer/ parser/ typecheck/ liveness/ regalloc/
         alg/ codegen/ integration/
  Makefile
```

Note the new pair `alg.[ch]`: the algorithm engine (sequence parsing, inversion, powers, simplification — Section 3.6), owned by Person 3 and ported to JS by Person 4. Makefile targets: `all`, `cubitc`, `test`, `test-<module>`, `oracle` (runs Python cross-validation), `clean`. Compile tests with `-std=c17 -Wall -Wextra -Werror -g -fsanitize=address`.

**Definition of done.** `make` builds a trivial `cubitc`; `make test` runs and passes zero tests; `make oracle` finds `python3` and runs `tools/algcheck.py --selftest`.

### 2.7 Task 0.7 — Agree the temporary-variable lowering point

The v1.0 condition compilation (`x = y`) requires a temporary register `Rt`. Temporaries must exist before liveness and register allocation, otherwise they are never allocated a disjoint algorithm.

**Decision (fixed now):**

> A desugaring pass runs between typechecking and liveness. It rewrites the AST so that (a) every `x = y` condition introduces a fresh synthetic `int` variable `__t<N>`, recorded in the type environment, and (b) every compound integer expression is flattened into a sequence of assignments through synthetic temporaries, leaving only the seven primitive forms of Table 1.

Ownership: Person 4 implements the pass (it serves codegen); Person 2 reviews it because liveness consumes its output. It lives in `codegen.c` as `desugar_program(Program*, TypeEnv*)` and is called from `main.c` in pipeline order.

### 2.8 Task 0.8 — Algorithm-engine ownership and the JS port rule

The algorithm engine (`alg.[ch]`) is implemented once in C by Person 3, then ported mechanically to JS by Person 4: same function names, same constants, same table contents copied verbatim. Neither side re-derives anything. Any change to `alg.c` after the port requires a matching commit to `index.html` in the same merge request — enforce by convention and code review.

### 2.9 Task 0.9 — Python oracle policy

`tools/algcheck.py` (our existing parser, extended minimally) exposes a CLI:

```
python3 tools/algcheck.py order "R U R' U'"   # prints 6
python3 tools/algcheck.py cycles "U"          # prints UF,UFL,...
python3 tools/algcheck.py state "U2"          # prints 48 ints
python3 tools/algcheck.py equal "U U" "U2"    # prints YES/NO
```

The C test drivers shell out to this (or compare against committed oracle output files, so CI does not need Python). The oracle is the referee whenever C, JS and Python disagree — and **if the oracle is wrong, fix it first, then the ports**. The Python code never runs inside `cubitc` or the emulator.

---

## 3. Design Decisions: Register Operations and Move Shortening

This section is normative. It resolves the open questions in v1.0 about what the register instructions mean at the cube level, how the compiler emits them, and how repeated algorithms are shortened. **Read it as a group before Phase 0 ends.**

### 3.1 Two levels of meaning

Every assembly instruction has:

- **(a)** an *abstract semantics* over register values `v_i ∈ Z_{K_i}` — what the compiler reasons about, and
- **(b)** a *physical realisation* as a move sequence applied to the cube — what the emulator animates and what a human with a real cube would perform.

The compiler only ever needs (a); the emulator implements (b) and keeps (a) as derived display state. The two are linked by the invariant:

```
cube state = Π_i  A_i ^ v_i      (disjoint cycle sets ⇒ the product order is irrelevant)
```

### 3.2 Decision D1: `Ri := Ri + n` (add immediate)

**Abstract:** `v_i ← (v_i + n) mod K_i`, exactly v1.0.

**Compiler obligations.** Codegen emits `add Ri #m` with `m = n mod K_i` already reduced (it knows `K_i` because codegen runs after regalloc). Negative immediates never appear in assembly: HL `x := x - 3` is desugared to primitive form `x := x + (-3)` and codegen emits `m = (K_i - (3 mod K_i)) mod K_i`. The assembly language therefore only ever carries `0 ≤ m < K_i`; the JS parser rejects anything else (defensive check, exit/console error).

**Physical realisation (emulator).** Given `add Ri #m`:

1. Reduce `m mod K_i` (belt and braces).
2. **Direction choice**: if `m ≤ K_i - m`, the raw realisation is `A_i` concatenated `m` times; otherwise it is `A_i^{-1}` concatenated `(K_i - m)` times, where `A_i^{-1}` is the sequence reversed with every move inverted (`R U R' U'` → `U R U' R'`). Both produce the same permutation because `A_i^{K_i} = id`.
3. Run the result through the simplifier (Section 3.6) before animating.

**Example:** `A_i = U`, `K_i = 4`, `add Ri #3` ⇒ direction choice gives `U'` (1 move) rather than `U U U` (3 moves); the simplifier would also have produced `U'` from the concatenation — the direction choice just keeps intermediate strings short.

### 3.3 Decision D2: `Ri := Ri + Rj` (add register) and copies

**Abstract:** `v_i ← (v_i + v_j) mod K_i`, `Rj` unchanged, exactly v1.0.

**Decision:** `add Ri Rj` is a *primitive* of the machine, not a macro. The emulator executes it by reading its own tracked `v_j`, computing `m = v_j mod K_i`, and then performing exactly D1's realisation of `add Ri #m`. This is legitimate because the emulator *is* the machine: its tracked `registers[]` array plays the role of an operator who can count.

**Physical-realisability footnote (report material, optional `--physical` flag).** A human with only a cube and no memory of `v_j` can still perform `add Ri Rj` destructively-then-restore using a third register `Rt` disjoint from both, in the style of a Minsky counter machine:

```
__cp_A: branch cycle(Aj) __cp_B   ; while Rj != 0:
        add Rj #(Kj-1)            ;   Rj -= 1
        add Ri #1                 ;   Ri += 1
        add Rt #1                 ;   Rt += 1
        goto __cp_A
__cp_B: branch cycle(At) __cp_end ; while Rt != 0:
        add Rt #(Kt-1)            ;   Rt -= 1
        add Rj #1                 ;   Rj += 1
        goto __cp_B
__cp_end:
```

Correct iff `K_t ≥ v_j_max`, i.e. `K_t ≥ K_j`; the allocator cannot guarantee that cheaply, so this lowering is not the default — it exists as an optional codegen mode behind `--physical` (stretch goal, Task 4.12) and as a worked example in the report showing the machine is "honest" (Turing-style counter-machine computation with no hidden digital state).

**HL-level copy `x := y`.** Compiles to: zero `Ri` (Decision D3), then `add Ri Rj`. Initialising copy `let x : int := y` may skip the zeroing only under the virgin rule (D4).

**Subtraction `x := x - y`.** Keep the v1.0 three-line pattern (`negate Rj` / `add Ri Rj` / `negate Rj`); it preserves `Rj` and needs no temporary. `negate` realises physically as: `m = (K_j - v_j) - v_j` steps in the appropriate direction — i.e. the emulator treats `negate Rj` as `add Rj #((Kj - 2*vj) mod Kj)` computed at execution time.

### 3.4 Decision D3: zeroing and constant re-assignment `Ri := n`

There is no constant-store instruction: the machine can only add. A register holding an unknown `v_i` is zeroed with a counting loop ("solve the register"):

```
__zero_M: branch cycle(Ai) __zero_done_M
          add Ri #1
          goto __zero_M
__zero_done_M:
```

This executes `K_i - v_i` iterations. Encapsulate as `emit_zero(cg, reg)`. `x := n` (re-assignment) is then `emit_zero` followed by `add Ri #n`.

> **Note for the report:** this is the one place the language pays for having no read-out instruction — constant stores are `O(K_i)` *executed* instructions (still `O(1)` *emitted* instructions).

### 3.5 Decision D4: the virgin-register optimisation (skip zeroing when provably zero)

The cube starts solved, so every register is zero before its first write. But register reuse (two variables with disjoint lifetimes sharing a register) and backward jumps (a `let` re-executed via `goto`/loop) both break the naive "let writes a fresh register" assumption.

**Decision** — codegen may omit `emit_zero` on a `let` initialisation iff **all** of:

1. this is the textually first emitted write to register `Ri` (track a `bool written[reg_count]` during codegen), **and**
2. no label statement and no loop header has been emitted yet at this point (track a `bool past_first_label`) — a cheap, sound proxy for "this instruction executes at most once, before anything that could re-enter it".

Otherwise emit the zero loop. This is conservative and easy to argue correct in the report; measuring how often it fires is a nice evaluation table.

### 3.6 Decision D5: move shortening (the algorithm simplifier)

**Question resolved:** when the emulator must perform `A_i` `m` times, do we replay the sequence `m` times or combine?

**Decision:** a three-stage pipeline, implemented once in `alg.c` (P3), ported to JS (P4):

**Stage 1 — direction choice.** As in D1: realise `A_i^m` as `min(m, K_i - m)` repetitions of `A_i` or `A_i^{-1}`.

**Stage 2 — peephole simplification to fixpoint.** Represent a sequence as an array of `(face, q)` pairs with `q ∈ {1, 2, 3}` quarter-turns clockwise (`U`=1, `U2`=2, `U'`=3). Apply these rewrite rules until no rule fires:

- **Merge:** adjacent moves on the same face combine: `q ← (q1 + q2) mod 4`; delete if 0. (`U U → U2`; `U U' → ε`; `U2 U2 → ε`; `U U2 → U'`.)
- **Opposite-face normalisation:** moves on opposite faces commute (`U D = D U`, similarly L/R, F/B). When two adjacent moves are on opposite faces and they are out of canonical face order (canonical: `U < D < L < R < F < B`), swap them. This bubbles same-face moves together across an intervening opposite-face move so Merge can fire: `U D U' → U U' D → D`.

**Termination:** each Merge strictly shortens the array; each swap strictly reduces the number of out-of-order opposite-face adjacent pairs without lengthening — so the loop terminates. Output is a canonical form for the rewrite system (good unit-test property: the simplifier is idempotent).

**Stage 3 — per-register power memo.** The emulator caches, per register `i`, the simplified string for `A_i^m` keyed by `m` (`powerCache[i][m]`), populated lazily. Cap the cache at `K_i ≤ 256` entries; beyond that compute on demand (orders that large will not appear in demos). The C compiler needs the same routine only for `--dump-regs` cosmetics and tests, so it shares `alg.c` but not the cache.

**Explicit non-goal (and stretch goal).** Stage 2 does not find the optimal sequence realising the permutation `perm(A_i)^m` — that is a cube-solver search. As a stretch (Task 3.10), reuse the IDA* machinery to search for any sequence producing the same `CubeState` as the simplified one with length bound = current length − 1, iterating downwards; cap the time budget. Benchmark Stage-2-only vs. Stage-3 search in the report. Default shipped behaviour: Stages 1–3 memo, no search.

**Correctness property (mandatory test).** For 1,000 random sequences of length ≤ 25: the cube state after the original equals the cube state after the simplified output (checked with `cube.c`), and the Python oracle agrees on both (`algcheck.py equal`). This single property test is the backbone of the algorithm engine's test suite.

### 3.7 Decision D6: where shortening is and is not applied

- **Emulator (always):** every realised `add`, `negate`, and `add_reg` animates the simplified sequence.
- **Compiler, alg literals:** `apply` expressions and alg-variable concatenations are simplified once at codegen time before being written into raw-move lines (so `apply x ++ y` with cancelling tails emits the short form).
- **Never across instruction boundaries:** we do not merge the realisations of consecutive assembly instructions, because branches must observe the cube exactly at instruction boundaries. State this invariant in a comment at the top of the JS `step()`.

### 3.8 Summary: the seven primitive assignment forms

| # | HL form (post-desugar) | Emitted assembly | Notes |
|---|---|---|---|
| 1 | `let x := n` (virgin) | `add Ri #n` | D4 |
| 2 | `x := n` | zero loop; `add Ri #n` | D3 |
| 3 | `x := y` | zero loop; `add Ri Rj` | D2 |
| 4 | `x := x + n` | `add Ri #(n mod Ki)` | D1 |
| 5 | `x := x - n` | `add Ri #((Ki-n) mod Ki)` | D1 |
| 6 | `x := x + y` | `add Ri Rj` | D2 |
| 7 | `x := x - y` | `negate Rj; add Ri Rj; negate Rj` | D2 |

*Table 1: Primitive forms after desugaring, and their compilation. Anything not of these shapes is rewritten through `__t` temporaries by the desugarer (Task 4.1). For form 3 with `let`, the zero loop is subject to D4.*

---

## 4. Pipeline Order (main.c)

`main.c` (nominally Person 1) wires the stages:

```
read file -> lexer -> parser -> typecheck -> desugar
         -> cfg_build -> liveness -> ig_build
         -> regalloc -> codegen -> write .asm
```

**Command line:** `./cubitc <input.chl> <output.asm>`, with flags `--dump-ast`, `--dump-desugar`, `--dump-cfg`, `--dump-liveness`, `--dump-ig`, `--dump-regs`, `--physical` (stretch). Each `--dump-*` prints the named intermediate to stdout and continues; the dumps are how the four of you debug each other's stage boundaries, so each is implemented as part of the owning person's tasks below.

---

## 5. Person 1 — Language Frontend (lexer, AST, parser)

Owns `lexer.[ch]`, `parser.[ch]`, `ast.h`, the AST printer, `main.c`, and `--dump-ast`.

### 5.1 Task 1.1 — ast.h, first and frozen

Write `ast.h` exactly as in v1.0 §6.2 (structs and enums only) and commit it on day 1: Persons 2 and 4 code against it immediately. Additions to the v1.0 definition:

- Every node carries `int line`, populated from the first token of the construct.
- The `eq_var` branch of `Cond` gains `char *temp_name;` (filled by the desugarer, Task 4.1; `NULL` until then). Agree this field now so `ast.h` never changes later.
- Constructors/destructor declared in `parser.h`: `stmt_new`, `expr_new`, `cond_new`, `program_free`. `program_free` recursively frees everything; all parser tests run under ASan.
- A `Program` also needs an insertion-friendly statement list: implement statement bodies as dynamic arrays with a shared `util.c` helper pair (`void push_ptr(void ***arr, int *count, int *cap, void *p)` and `void insert_ptr(...)`, the latter needed by the desugarer to splice temporaries before the current statement).

**Definition of done.** `ast.h` compiles standalone (`gcc -fsyntax-only`); P2 and P4 have signed off field names in a 15-minute review.

### 5.2 Task 1.2 — Token definitions and lexer state

Implement `TokenType`, `Token`, `Lexer` per v1.0 §6.1. Ownership rule: `lexer_next` heap-allocates `Token.text`; the consumer frees with `lexer_free_token`. `TOK_EOF` and single-character symbols may carry `text == NULL` (printer must handle it). Decide and document the behaviour of `lexer_next` after EOF: it returns `TOK_EOF` forever (idempotent), never crashes.

### 5.3 Task 1.3 — Core scanning loop

Implement `lexer_init` and `lexer_next` for everything except alg-context behaviour:

1. Whitespace with line/col bookkeeping; treat `\r\n` as one newline (test files may come from Windows).
2. Comments: `--` to end of line (v1.0 examples use it) and `//` to end of line. No block comments (decision: out of scope; document).
3. Symbols, longest-match-first: `:=` before `:`, `++` before `+`.
4. Integer literals: decimal; value in `int_val`; reject `> 2^31 - 1` with `die(1, "lexer", ...)`. No hex in HL source (hex exists only in the coursework's A64 assembler, not CuBit — decision: out of scope).
5. String literals: double quotes, single line, no escapes; unterminated ⇒ lexer error with the line the quote opened on.
6. Identifiers/keywords: `[a-zA-Z_][a-zA-Z0-9_]*`, then a static keyword table (`let int alg while if else goto input output apply ord solved not`); else `TOK_IDENT`. Reject leading `__` (Task 0.5 rule 1) here with a dedicated message.

**Definition of done.** Driver `tests/lexer/dump_tokens.c` prints `LINE:COL TYPE "text"` per token; golden tests pass for at least 6 sources jointly covering every token type, both comment styles, and CRLF input.

### 5.4 Task 1.4 — Alg-context mode

Implement `lexer_set_alg_context`. With the flag set, `lexer_next` applies this rule before the identifier rule:

> If the current char is one of `UDLRFB` and the longest identifier starting here is exactly that letter, that letter + `2`, or the letter is followed by `'`: emit `TOK_MOVE` with text `"U"`, `"U2"` or `"U'"`. Otherwise fall through to the identifier rule (so `Rx` lexes as identifier `Rx` even in alg context, allowing multi-character alg variables inside concatenations).

**Pitfalls.**

- (a) `U2x` must lex as identifier `U2x`, not `U2`+`x` — hence "longest identifier" look-ahead.
- (b) `'` is not an identifier char, so `U'` is unambiguous.
- (c) `lexer_peek` must respect the flag at call time; simplest correct implementation: run `lexer_next` on a struct copy of the lexer.
- (d) The flag must also be honoured by the parser's two-token lookahead — if a token was buffered under the wrong mode, you get heisenbugs; rule: the parser only toggles the flag when its lookahead buffer is empty (assert this).

**Definition of done.** Golden tests toggling the flag mid-stream; the same text lexes differently in the two modes; the four pitfall cases above are each a named test.

### 5.5 Task 1.5 — Parser scaffolding

`parser_init`, `parser_peek`, `parser_advance`, `parser_check`, `parser_match`, `parser_expect`, `parser_error` (calls `die(2, "parse", ...)` with the expected vs found token names — write a `token_type_name()` table once). Two tokens of lookahead, used only at statement heads.

### 5.6 Task 1.6 — Statement parsing and the context-flag protocol

`parse_program` loops `parse_stmt` to EOF. Dispatch on the first token; for `TOK_IDENT`, peek the second (`:=` ⇒ assign, `:` ⇒ label, else parse error).

The context-flag protocol — the heart of the frontend — is fixed as follows; implement it as a single helper `Expr *parse_rhs(Parser*, TypeKind declared_or_unknown)`:

1. `let x : int := e` — call `parse_rhs(TYPE_INT)`: parse an int expression, flag off throughout.
2. `let x : alg := e` and `apply e` — set the flag, parse an alg expression, clear the flag.
3. `x := e` (type unknown to the parser) — decide by the first token, flag initially off:
    - `INT_LIT`, `(`, `-`, `ord` ⇒ int expression.
    - `IDENT` ⇒ consume it, then peek: `++` ⇒ alg concat whose first term is that variable (set flag for subsequent terms); `+`/`-` ⇒ int expression continuing from that variable; end-of-statement ⇒ a bare `EXPR_VAR` with `resolved_type = TYPE_UNKNOWN` (P2 settles it).
    - Anything else ⇒ parse error. **Consequence (document as a language rule in the report):** an alg literal cannot be the RHS of a plain re-assignment to an alg variable unless wrapped in a concat with a variable or done at `let`. If the group dislikes this, the alternative is to thread variable types into the parser — rejected for coupling; revisit only if a test program actually needs it.

End-of-statement detection: CuBit-HL has no semicolons; a statement ends where the next statement (or EOF or `}`) begins. Since expressions are not newline-sensitive in the grammar, adopt the rule: an expression is parsed maximally; the statement ends when the next token cannot extend the expression. Write this in a comment with three worked examples.

**Definition of done.** Each v1.0 §4.2 statement form round-trips parse → print against a golden file; the bare-variable/concat/int trichotomy has a dedicated test trio.

### 5.7 Task 1.7 — Expression and condition parsing

- **Int expressions:** left-assoc `+`/`-`, one precedence level; unary minus binds tighter (`-x + y` is `(-x) + y`); parentheses; `ord(IDENT)` — inside `ord(...)` the flag is off (the argument is a variable name, never a literal).
- **Alg expressions:** `alg_term (++ alg_term)*`; an alg literal is a maximal run of `TOK_MOVE`s joined with single spaces into one `EXPR_ALG_LIT` string (canonical spacing matters: P3's `alg_parse` and the oracle both assume single-space separation).
- **Conditions** per the v1.0 grammar; not recursive; inside `solved[...]` the flag must be off (piece labels like `UF` would otherwise lex as moves) — pieces are plain identifiers stored as strings; validation is P2's. `x = y` vs `x = 5`: peek after `=`.

### 5.8 Task 1.8 — AST printer and --dump-ast

`ast_print_program`: indented, one node per line, printing `resolved_type` when not `UNKNOWN` (so the same dump serves P2's post-typecheck goldens). Stable format — it is the golden format for your tests, P2's regression tests, and P4's `--dump-desugar`.

### 5.9 Task 1.9 — main.c and stage wiring

Implement argument parsing, the flag set, file reading (`read_entire_file` in `util.c`), and the pipeline calls, stubbing stages that do not exist yet behind `#ifdef` or weak no-op defaults so the binary always links during parallel development.

### 5.10 Task 1.10 — Frontend fuzz pass

A 50-line shell/python fuzzer that mutates valid programs (delete/duplicate/swap random tokens) and asserts `cubitc` never crashes (ASan-clean) and always exits 0–5 with a `[stage]`-prefixed message. Run nightly-ish; file any crash as a test case.

**Tests.** Minimum suite: every statement kind; if/while nested 3 deep; every expression and condition form; v1.0 §11.3; 8 malformed programs asserting exit code 2 and the correct line number; the context-flag pitfall quartet; fuzz corpus committed.

**Report material.** The context-sensitive lexing problem and the parse-without-types RHS trichotomy — both are genuinely interesting frontend design points.

---

## 6. Person 2 — Typechecker, Liveness, Interference Graph

Owns `typechecker.[ch]`, `liveness.[ch]` (CFG, liveness, interference graph), `--dump-cfg`, `--dump-liveness`, `--dump-ig`; reviews P4's desugarer (Task 4.1).

### 6.1 Task 2.1 — Type environment

`TypeEnv` as a dynamic array of `VarEntry` (v1.0 §7.1.1); linear lookup is fine. Predeclare `R0` as `KIND_INT` in `typeenv_init` (Task 0.2). Add `int typeenv_index(TypeEnv*, const char*)` returning a stable small-integer id per variable; liveness uses ids internally, strings only at API boundaries. The env must keep accepting additions after typechecking (the desugarer registers `__t` temporaries).

### 6.2 Task 2.2 — Label pre-pass

Recursively collect every `label_stmt` name (including inside if/while bodies) into a string set; duplicate label ⇒ `[typecheck]` error. The set validates `goto` targets in the main walk.

### 6.3 Task 2.3 — Alg compile-time environment

Alg variables are compile-time entities (their values are strings; codegen substitutes them — Task 4.6). To support `ord(x)` typing and later constant-folding, additionally record, for each alg variable, whether its full value is a compile-time-known string: `let a : alg := <literal or known-concat>` is known; `input` never targets alg variables, so in the current language every alg variable is compile-time known — assert this invariant, and structure the code so the assertion is one line to delete if the language later gains runtime algs.

### 6.4 Task 2.4 — Main typecheck walk

Single recursive walk implementing the v1.0 §7.1.3 rules. Specific behaviours:

- Populate `resolved_type` on every `Expr`, including resolving `TYPE_UNKNOWN` bare variables (Task 1.6) by lookup.
- Undeclared variable use: error. Use of a variable before its declaration point (legal flow via `goto` notwithstanding): error — scoping is textual per v1.0 §4.5.
- Enforce Task 0.5 rule 2 (no alg vars named U/D/L/R/F/B) at the `let` site.
- `solved[pieces]`: each piece must satisfy `piece_from_string() != -1`; report which piece.
- `x := e`: `x` declared, types match; assigning to an alg variable after declaration is legal only for known-alg RHS (Task 2.3).
- Error format exactly `[typecheck] line N: msg`, exit 3, first error fatal. Match the four v1.0 §7.1.4 example messages verbatim — they are golden tests.

**Definition of done.** 12+ negative goldens (one per rule, including the alg-name rule and a bad piece label) and 5 positive programs whose `--dump-ast` (with types) matches goldens.

### 6.5 Task 2.5 — CFG construction

Implement `cfg_build` (v1.0 §7.2.1) in two phases:

1. **Flatten:** recursive walk emitting one `CFGNode` per atomic statement; if/while each get a node for their condition (node's `stmt` points at the if/while `Stmt`); bodies get their own nodes. Record, per label, the node of the first statement after it (or exit).
2. **Link:** edges per the v1.0 rules; maintain a worklist of dangling exits when joining if/else tails; synthetic entry and exit nodes; a final `goto` simply leaves exit unreached from that path.

**Pitfalls.**

- (a) `if` with no `else`: false-edge straight past the `if`.
- (b) empty `while` body: true-edge loops to the condition node itself.
- (c) trailing label maps to exit.
- (d) `goto` into a loop body is legal — the label map built during flattening handles it with no special case; add a test proving it.
- (e) The CFG is built after desugaring, so condition nodes may reference `__t` temporaries — no special-casing, they are ordinary variables by then.

**Definition of done.** `--dump-cfg` prints `node <id>: <stmt summary> -> succ ids`; goldens for straight-line, if, if/else, while, nested while, goto-into-loop, and v1.0 §11.3.

### 6.6 Task 2.6 — Use/def and liveness fixpoint

Live sets as `uint64_t` bitsets indexed by `typeenv_index` (assert ≤ 64 int variables; `die(5,...)` otherwise — alg variables are compile-time only and are excluded from liveness entirely; document why). Use/def per v1.0 §7.2.3 plus the desugared forms: the `x = y` condition node uses `{x, y}` and defs its `__t` temp (the temp is written by the comparison pattern); the temp is also used by it. `output` uses `R0`; `input` defs `R0`.

**Fixpoint:** full reverse-id passes until no bitset changes (a worklist is an optional optimisation; measure both for the report if curious). Convert bitsets to string arrays only at the `LivenessResult` boundary.

**Pitfalls.** `def` kills only on the flow into `live_in`; follow the equations literally. Free correctness dividend: any variable (other than `R0`) live-in at entry was used before definition — print a `[liveness]` warning, not an error.

**Definition of done.** `--dump-liveness` prints per node `in={...} out={...}`; one example verified by hand on paper and committed as `tests/liveness/handworked.md`.

### 6.7 Task 2.7 — Interference graph

`ig_build`, `ig_add_edge`, `ig_interfere`, `ig_find_node` per v1.0 §7.3, plus the standard refinement: at a node defining `x`, also add edges `x`–`y` for every `y ∈ live_out \ {x}` even if `x` is not itself live-out (dead stores must still own a register at the write). Exclude `R0` from the graph; its disjointness is enforced unconditionally by P3 (Task 3.7).

**Definition of done.** `--dump-ig` prints a name-sorted adjacency list; a golden test where two variables with sequential lifetimes get no edge — the test that proves register reuse will happen.

### 6.8 Task 2.8 — Desugarer review (with P4)

Joint half-day: walk P4's `desugar_program` against Table 1; re-run all liveness goldens on desugared output; confirm every `__t` appears in the `TypeEnv` and the IG. Sign-off recorded in the merge request.

**Tests.** As listed per task; additionally one adversarial program combining `goto`-into-loop with a `x = y` condition, hand-checked end to end through `--dump-cfg`, `--dump-liveness`, `--dump-ig`.

**Report material.** The hand-worked liveness example; the dead-store edge refinement; why alg variables are exempt from liveness.

---

## 7. Person 3 — Cube Model, Algorithm Engine, Register Allocator, IDA*

Owns `cube.[ch]` (facelet simulation), `alg.[ch]` (the algorithm engine of Section 3.6), `regalloc.[ch]`, `--dump-regs`, and the C side of the Python-oracle harness.

### 7.1 Task 3.1 — Facelet numbering scheme

Fix a numbering of the 48 non-centre facelets: 8 per face, faces in order `U(0–7)`, `D(8–15)`, `L(16–23)`, `R(24–31)`, `F(32–39)`, `B(40–47)`; within a face, row-major from the top-left with the viewing orientation documented per face (e.g. "U viewed from above with F toward you; F viewed head-on with U above"; fix all six). Draw the scheme as an ASCII net diagram in a comment at the top of `cube.h`. This diagram is a shared contract in all but name: P4 copies it verbatim into `index.html`, and the Python oracle's numbering is remapped to it (Task 3.9) — never the other way around.

**Definition of done.** The diagram exists; P4 and the oracle owner have initialled it in review.

### 7.2 Task 3.2 — Move tables and core cube API

1. State: `uint8_t state[48]`, `state[i]` = the facelet currently at position `i`; identity is `state[i] = i`.
2. Hand-derive permutation arrays for the six clockwise quarter-turns only. Derive each `X2` and `X'` at init by composing `X` with itself — never hand-write 18 tables. Composition: `out[i] = a[b[i]]`; get the order right and lock it with the test "`U` then `U'` is identity".
3. API per v1.0 §8.5.4: `cube_apply_move`, `cube_apply_sequence`, `cube_is_identity`, `compute_order` (cap 1260; `die(5,...)` if exceeded — it means a table bug).

**Pitfalls.** Verifying the hand-written tables is the whole game. Required tests:

- (a) each quarter-turn^4 = identity;
- (b) each `X X'` = identity;
- (c) `compute_order` on a battery of sequences checked against the Python oracle, not against remembered values — commit the oracle outputs as the goldens;
- (d) `U D` vs `D U` produce equal states (commutativity of opposite faces, which Stage 2 of the simplifier relies on);
- (e) 500 random sequences: C state equals oracle state.

**Report material.** How composition-derivation plus property tests caught table bugs (keep one real bug as an anecdote).

### 7.3 Task 3.3 — Cycle sets

`CycleSet` as a `uint32_t` bitmask over `PieceLabel` (disjoint is `(a&b)==0`); `cycleset_from_alg`: apply the sequence once to identity; a piece is in the set iff any of its facelets is displaced. You need a static 48-entry table mapping facelet position → `PieceLabel`; derive it from Task 3.1's diagram and have a teammate independently spot-check 10 entries.

**Pitfalls.** A cubie twisted/flipped in place has displaced facelets and is in the cycle set — correct for both branch semantics and register encoding, which are facelet-level. Add a test using a corner-twisting algorithm if you can find a short one, else note it.

**Definition of done.** `cycleset_from_alg("U")` equals the R0 contract set (asserted at `regalloc_init` too); `"U"`/`"D"` disjoint; `"R"`/`"U"` not.

### 7.4 Task 3.4 — Algorithm engine: parsing, inversion, powers (alg.[ch])

This replaces the Python parser inside the toolchain (Section 2.9). API:

```c
typedef struct { uint8_t face; uint8_t q; } Move; /* q in 1..3 */
typedef struct { Move *m; int len, cap; } Alg;

bool alg_parse(const char *text, Alg *out);          /* SiGN notation */
char *alg_to_string(const Alg *a);                   /* single-spaced */
void alg_invert(const Alg *in, Alg *out);            /* reverse + q->4-q */
void alg_concat(Alg *dst, const Alg *src);
void alg_power_realise(const Alg *a, int m, int K, Alg *out);
                       /* Stage 1 of D5: direction choice */
void alg_simplify(Alg *a);                           /* Stage 2 of D5 */
```

`alg_parse` accepts exactly: face letter, optional `'` or `2`, single-space separated; anything else returns `false` (callers convert to the appropriate `die`). Port the *behaviour* of the Python parser, not its code; where the Python version is more permissive (multiple spaces, lowercase?), the C version is the stricter law and the oracle CLI is updated to normalise its input the same way.

**Definition of done.** Round-trip property: `alg_to_string(alg_parse(s))` canonicalises; `alg_invert` twice is identity; parse-reject tests for malformed input.

### 7.5 Task 3.5 — Simplifier (Stage 2) and its proof obligations

Implement `alg_simplify` per Section 3.6: repeat {merge adjacent same-face; canonical-order swap adjacent opposite-face} until a full pass changes nothing.

- Implement the pass over the `Move` array in place with a write cursor; a pass is `O(n)`; total is `O(n^2)` worst case — fine for our lengths.
- **Mandatory property test (the D5 correctness property):** 1,000 random sequences, length ≤ 25: cube state of simplified = cube state of original (via `cube.c`), and the oracle agrees (`algcheck.py equal`); plus idempotence: simplifying twice changes nothing.
- **Named unit tests:** `U U → U2`; `U U' → ε`; `U2 U2 → ε`; `U D U' → D`; `R U U' R' → ε` (cancellation cascades through the gap left by a deletion — make sure your write-cursor pass re-examines the new adjacency, or just rely on the outer fixpoint loop).

**Report material.** The rewrite system, its termination argument, and measured shortening ratios on the integration programs (table: naive repetition length vs. simplified length for each `add` executed in `hello_counter`).

### 7.6 Task 3.6 — Register table

`RegTable` growth; `regalloc_init` pre-populates index 0 from the R0 contract, computing its cycle set with your own `cycleset_from_alg` and asserting it matches the contract (catches cube-model regressions at startup of every compile).

### 7.7 Task 3.7 — IDA* disjoint-algorithm search

Implement v1.0 §8.5 with these concretisations:

- **Alphabet:** the 18 moves, iterated in a fixed documented order (determinism — P4's codegen goldens depend on it).
- **Pruning:** never turn the same face twice in a row; for opposite-face pairs require canonical order (no `D` directly after `U`, etc.) — cuts branching from 18 to ~13 and guarantees the emitted algorithm is already Stage-2 canonical.
- Maintain a running `CubeState` along the DFS path, **not** a `CycleSet`: displaced-piece sets are not monotone along a prefix (a piece can return home), so pruning on the prefix's cycle set is unsound. Required behaviour: test forbidden-disjointness (cycle set computed from the state) at leaf nodes only. The interior-node prune is an optional optimisation you may enable only with a written safety argument or a benchmark showing it changes no test outcome — this corrects the v1.0 §8.5.3 pseudocode; flag it in the report.
- **Leaf acceptance:** `compute_order(seq) > 1` and cycle set disjoint from forbidden.
- `MAX_SEARCH_DEPTH = 6`; exhaustion ⇒ v1.0 §8.5.5 message via `die(4, "regalloc", ...)`.

**Definition of done.** Forbidding R0's set yields a disjoint order-`> 1` algorithm in under a second; iterating with the growing union finds ≥ 3 mutually disjoint registers before exiting 4; the same inputs always yield the same outputs across runs.

### 7.8 Task 3.8 — Graph colouring driver

`regalloc_run` per v1.0 §8.4: IG nodes sorted by degree descending (ties alphabetical); per node, gather neighbour colours and cycle sets; scan the table (skipping R0) for an eligible existing register; else IDA* with `forbidden = R0.cycles ∪ (union of neighbour cycles)`. R0's set is always forbidden, so user registers can never clobber I/O. After colouring, run the invariant checker: for every IG edge, assigned cycle sets disjoint; every register disjoint from R0; violation ⇒ `die(5, "internal", ...)`.

**Definition of done.** `--dump-regs` prints `var -> Ri alg="..." K=n C={pieces}`. Goldens: reuse across disjoint lifetimes; no sharing across an edge; a pressure program exits 4 with the exact message.

### 7.9 Task 3.9 — Oracle harness

Extend `tools/algcheck.py` with the CLI of Task 0.9 and an internal remap table from its native facelet numbering to Task 3.1's. Write `tests/alg/run_oracle.sh`: generates random sequences (fixed seed), runs C drivers and the oracle, diffs. Commit the fixed-seed outputs so `make test` passes without Python; `make oracle` regenerates them.

### 7.10 Task 3.10 — (Stretch) solver-based shortening (Stage 3+)

Reuse the IDA* DFS to search for any sequence reproducing a target `CubeState` with length bound = current length − 1, iterating downward under a wall-clock budget (e.g. 50 ms per query). Wire it behind a flag in `alg.c`; benchmark vs. Stage 2 for the report. Do not start before everything above is green.

---

## 8. Person 4 — Desugaring, Code Generator, Browser Emulator

Owns the desugarer and `codegen.[ch]`, `--dump-desugar`, `emulator/index.html` (including the JS port of `cube.c`/`alg.c`), `emulator/TESTING.md`.

### 8.1 Task 4.1 — Desugaring pass

`desugar_program(Program*, TypeEnv*)` per Task 0.7, producing only Table 1 forms:

1. Walk statements with an insertion cursor (`insert_ptr`, Task 1.1).
2. For an assignment `x := e` where `e` is not a primitive form: recursively reduce. Reduction rules (apply innermost-first): `e1 + e2` with both non-atomic ⇒ `let __tN := e1` (recursively desugared), then rewrite as `__tN + e2`; mirror for `-`; `-e` non-atomic ⇒ temp then negate; `x := y + z` (neither operand is `x`) ⇒ `x := y` (form 3) then `x := x + z` (form 6/4). `ord(a)` folds to an integer literal here (P3's `compute_order` on the known alg string — Task 2.3 guarantees it is known).
3. For each `x = y` condition: allocate `__tN`, store in `Cond.temp_name`, register as `int` in the `TypeEnv`.
4. Number temporaries globally (`__t0`, `__t1`, ...); never reuse names (the allocator does the reuse for you via liveness).

**Definition of done.** `--dump-desugar` (AST printer on the rewritten program) shows only primitive forms; P2's liveness goldens re-pass on desugared output; P2 sign-off (Task 2.8).

**Pitfalls.** Desugaring runs before regalloc, so it must not need `K_i`; note that form 5's modular reduction is deferred to codegen precisely for this reason.

### 8.2 Task 4.2 — Codegen scaffolding

`CodeGen` struct; `codegen_init`; `emit` (varargs; prefixes `line_counter`, increments by 10); `fresh_label(prefix)` → `__<prefix>_<n++>`; `reg(cg, var)` wrapping `regalloc_lookup`/`get`; `cycle_list(reg)` returning the comma-joined (no spaces) piece list of a register's cycle set — the only way branch piece lists are ever produced.

### 8.3 Task 4.3 — emit_zero and the virgin tracker

`emit_zero(cg, reg)` emitting the D3 loop with fresh labels. Virgin tracking per D4: `bool written[]` sized to the `RegTable`, `bool past_first_label`; set the latter when the first label or while/if-generated label is emitted. Unit-test the three cases: virgin `let` (no loop), post-label `let` (loop), reused register's `let` (loop).

### 8.4 Task 4.4 — Primitive-form compilation

Implement Table 1 exactly, including the `(K_i - n) mod K_i` reduction for form 5 and the negate/add/negate sandwich for form 7. Modular reduction of every emitted immediate (D1) happens here and only here.

**Definition of done.** Golden `.asm` per form, reviewed against Section 3 by one teammate.

### 8.5 Task 4.5 — Condition, if, while compilation

`compile_cond(cg, cond, Ltrue, Lfalse)` recursively; `not` swaps labels; then the v1.0 §9.1.4–9.1.5 skeletons with `fresh_label`.

- **`x = n`:** emit `add Ri #((Ki-n) mod Ki)`; `branch cycle(Ai) <Ltrue_stub>`; restore `add Ri #n`; fall through toward `Lfalse`. **Restore-on-both-paths rule:** the true path must also restore — emit a stub label `Ltrue_stub` whose body is `add Ri #n; goto Ltrue`. (The v1.0 4-line pattern restores only on fall-through; this fixes it. Skipping the restore when liveness says `x` is dead at `Ltrue` is an optional optimisation — implement the always-restore version first.)
- **`x = y`:** zero `__t` (it may be reused — use `emit_zero` subject to D4), copy-and-subtract per v1.0 §9.1.3 using the temp from `Cond.temp_name`, branch on the temp's cycle set; clear the temp on both paths via the same stub-label trick (or simply leave it dirty and rely on `emit_zero` at its next use — decide once, document; the leave-dirty option is simpler and sound because every use of a temp is preceded by zeroing — **adopt leave-dirty**).
- **`solved[...]`:** direct branch on the listed pieces.

**Definition of done.** Goldens for each condition form (incl. nested `not`), if, if/else, while, while-with-`x = y`.

### 8.6 Task 4.6 — I/O, apply, and alg substitution

`input "s"` → `input "s"`; `output` → `output A0-string`; `output "s"` → `output "s" A0-string`. `apply e`: resolve alg variables from the compile-time string map (built from `let ... : alg` declarations during codegen), concatenate, run `alg_simplify` (D6), emit one raw-moves line. Empty result after simplification: emit nothing (and test it).

### 8.7 Task 4.7 — Assembly grammar (EBNF) and JS parser

Write the EBNF of the Task 0.4 format as a comment block at the top of `index.html` first; then implement `parse_assembly(text)` against it producing the v1.0 §9.2.3 instruction objects, plus a label→index pre-pass. Reject malformed lines and out-of-range immediates (D1's `0 ≤ m < Ki` check happens at run start, once registers are known — i.e. after the register manifest is loaded, Task 4.8). Console-log and refuse to run on any reject.

### 8.8 Task 4.8 — The register manifest

The emulator must know each `Ri`'s `Ai`, `Ki`, and cycle set. Two sources, both supported:

- **(a) manifest header:** codegen emits leading comment lines `; reg 1 alg="R U R' U'" order=6` which `parse_assembly` consumes (extend the EBNF; the C emitter and JS parser change in the same commit per Task 0.8);
- **(b) manual entry** in the register panel for hand-written assembly.

`R0` is always present from the contract constants.

### 8.9 Task 4.9 — JS port of cube.c and alg.c

Mechanical port per Task 0.8: copy the move tables, facelet diagram, piece-mapping table, and the `alg_*` functions (`Uint8Array(48)`; `Move` as `{face, q}`). Include `computeOrder`, `cyclesetFromAlg`, `algInvert`, `algPowerRealise`, `algSimplify`, and the per-register `powerCache` (D5 Stage 3). Validate with an in-page self-test button that runs the committed fixed-seed oracle vectors (export them from Task 3.9 as a JS array) and reports pass/fail in the console panel.

### 8.10 Task 4.10 — Emulator core (step semantics)

State per v1.0 §9.2.2 plus `cubeState: Uint8Array(48)` as the source of truth; `registers[]` is derived display state updated alongside. `step()`:

- **`add_imm m`:** realise via D1 (direction choice + simplify, through the cache); apply to `cubeState`; update `v_i`; animate.
- **`add_reg j`:** `m = v_j mod K_i`; then exactly `add_imm m` (D2).
- **`negate`:** `add_imm ((Ki - 2*vi) mod Ki)` computed now (D2).
- **`branch`:** every listed piece has all facelets home in `cubeState`; empty list checks all 48; jump via label map else `pc++`.
- **`moves`:** simplify, apply, animate; warn on console if the sequence's cycle set intersects any register's cycle set (nice-to-have; cheap with `cyclesetFromAlg`).
- **`input`:** pause; modal prompt; validate with `algParse`; set `A0`/`K0`/`v0` per v1.0 §9.2.6; in the emulator, apply the sequence to `cubeState` on the user's behalf and say so on screen.
- **`output`:** animate `A0` until `R0`'s pieces are solved, counting; print `<label>: <count>`; this also zeroes `v0` — assert the tracked `v0` agrees with the count (`count = (K0 - v0) mod K0`) and console-error on mismatch (a free consistency check between the two levels of meaning).

> Invariant comment at the top of `step()`: shortening never crosses instruction boundaries (D6).

### 8.11 Task 4.11 — Roofpig, controls, panels

`animate_moves` with the clone-and-reinit fallback (v1.0 §9.2.5). Keep logical state in `cubeState`; treat Roofpig as a renderer of the cumulative move-history string (append each step's realised, simplified sequence; re-init with full history). Run `algSimplify` over the history too before re-init to bound replay length, and cap it (e.g. 4,000 moves) with a "display resync" that re-initialises Roofpig from a setup sequence if exceeded — document the cap in `TESTING.md`. Controls: Step / Run / Reset, `STEP_DELAY_MS` slider (default 400), register panel (`Ri`, `Ki`, `Ai`, `vi` rows from `update_display()`), current-line readout, console panel.

### 8.12 Task 4.12 — (Stretch) --physical codegen mode

The D2 counter-machine lowering of `add Ri Rj` behind `--physical`, including the allocator-side constraint request `Kt ≥ Kj` (coordinate with P3: a one-field extension to the IDA* call asking for minimum order). Only after everything else is green; its real value is the report discussion either way.

**Tests.** Golden `.asm` for every primitive form, every condition form, if/else, while, §11.3; `emulator/TESTING.md` manual script stepping `hello_counter` with expected register-panel values and console transcript at each step; the in-page self-test green against oracle vectors.

**Report material.** The zeroing problem and D3/D4; restore-on-both-paths; the two-level semantics and the output-count consistency check; Roofpig statelessness and the history-cap workaround.

---

## 9. Integration Plan and Milestones

### 9.1 Stage-boundary handshakes

Integration happens at named boundaries, each with a dump flag and an owner pair:

| Boundary | Artefact | Flag | Producer | Consumer |
|---|---|---|---|---|
| B1 | AST | `--dump-ast` | P1 | P2, P4 |
| B2 | Annotated AST + TypeEnv | `--dump-ast` | P2 | P4 |
| B3 | Desugared AST | `--dump-desugar` | P4 | P2 |
| B4 | Interference graph | `--dump-ig` | P2 | P3 |
| B5 | RegTable | `--dump-regs` | P3 | P4 |
| B6 | Assembly + manifest | (the `.asm`) | P4 codegen | P4 emulator |
| B7 | Cube/alg engine | oracle vectors | P3 | P4 (JS port) |

At each boundary the producer commits 3 example dump files into `tests/<stage>/examples/` before the consumer starts; the consumer's unit tests parse those files (or link against the producer's module once it exists). B7's artefact is the fixed-seed oracle vector set (Task 3.9), consumed by the emulator self-test (Task 4.9).

### 9.2 Suggested timeline

- **Day 1:** Phase 0 complete (all); Section 3 signed off; `ast.h` frozen (P1); facelet diagram drafted (P3).
- **End of week 1:** P1 lexer+parser green; P2 typechecker green against P1's parser; P3 cube model + cycle sets + `alg.c` parse/invert green against the oracle; P4 EBNF written, emulator parser/step running hand-written assembly with manually entered registers.
- **Mid week 2:** P2 CFG+liveness+IG; P3 simplifier + IDA* + colouring; P4 desugarer reviewed (Task 2.8) + codegen for straight-line programs; first end-to-end compile of a no-branch program; emulator runs it via the manifest.
- **End of week 2:** Conditions, loops, I/O end-to-end; `hello_counter` compiles, runs in the emulator, recorded as the demo. Feature freeze; stretch tasks (3.10, 4.12) only if green.
- **Final days:** `make test` green; report sections drafted from the per-task Report material notes; presentation rehearsed, including one deliberately failing test to discuss.

### 9.3 Integration tests (tests/integration/)

Each is a `.chl` source plus golden `.asm` and an expected emulator console transcript:

1. **`arith.chl`** — declarations and all seven primitive forms, including a re-assignment that must trigger the zero loop and a virgin `let` that must not (grep the golden for `__zero`).
2. **`desugar.chl`** — a three-operator expression and an `x = y` condition; golden `--dump-desugar`.
3. **`branchy.chl`** — if/else over `x = n` and `solved[...]`; verifies restore-on-both-paths by running both arms in the emulator and checking `x` afterwards.
4. **`loop.chl`** — while with `not`, register reuse across disjoint lifetimes (assert two variables share an `Ri` in `--dump-regs`).
5. **`hello_counter.chl`** — v1.0 §11.3 verbatim.
6. **`pressure.chl`** — forces at least one IDA* invocation; asserts success and prints the discovered algorithm.
7. **`too_many.chl`** — exhausts the cube; asserts exit 4 and the §8.5.5 message.
8. **`shorten.chl`** — an `apply` with cancelling concatenation and an `add` with `m > Ki/2`; the golden `.asm` and the emulator's logged realised sequences show the simplifier and direction choice firing.

Run via a small shell script from `make test`: compile, diff `.asm` and each present `--dump-*` against goldens.

---

## 10. Report and Presentation Hooks

Collected from the per-task Report material notes:

- **P1:** context-sensitive lexing; the typeless-parser RHS trichotomy and the language rule it induces.
- **P2:** hand-worked liveness; dead-store interference edges; why alg variables are exempt from liveness.
- **P3:** the unsound prefix-cycle-set prune found in v1.0's IDA* pseudocode and its leaf-check fix; the simplifier rewrite system with its termination argument; shortening-ratio measurements; composition-derived move tables and the property tests that validated them against the Python oracle.
- **P4:** the zeroing problem (no constant store in a group-theoretic machine) and the D3 loop with the D4 virgin optimisation (with fire-rate measurements); restore-on-both-paths; the two-level semantics and the output-count consistency check; Roofpig statelessness and the simplified-history workaround.
- **All:** Section 3 as a designed-up-front semantics (examiners reward this); the oracle architecture (Python as referee, C as law, JS as mechanical port); the disjointness invariant checker; how dump flags enabled four-way parallel development.