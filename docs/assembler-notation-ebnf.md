# CuBit Assembly Language — Syntax Reference

> **Canonical definition.** This file is the authoritative description of the CuBit assembly
> text format (Task 0.4 / Task 4.7). The EBNF below is reproduced verbatim as a comment
> block at the top of `emulator/index.html`. Both the C code emitter (`codegen.c`) and the
> JS parser (`parse_assembly`) are written against this grammar. Any change requires a
> group vote and simultaneous commits to this file, `index.html`, and `codegen.c`.

---

## 1. File-Level Structure

An assembly file is a sequence of lines followed by end-of-file.
Blank lines and lines whose first non-whitespace character is `;` are comments and are ignored
by the parser (the register-manifest header uses `;`-prefixed comment lines — see §4).

```ebnf
program      = { manifest-line } { line } EOF ;
line         = line-number "  " instruction newline
             | line-number "  " label-def   newline ;
line-number  = digit { digit } ;
newline      = "\n" | "\r\n" ;
```

**Rules:**
- Line numbers are **strictly increasing in steps of 10** (10, 20, 30, …).
- Exactly **two spaces** separate the line number from the instruction text.
- The parser rejects any line whose number is not exactly 10 greater than the previous.

---

## 2. Labels

A label is emitted as its own numbered line. The label name is followed immediately by `:` with
no space before the colon.

```ebnf
label-def    = identifier ":" ;
identifier   = letter { letter | digit | "_" } ;
letter       = "a"…"z" | "A"…"Z" | "_" ;
digit        = "0"…"9" ;
```

**Rules:**
- Compiler-generated labels begin with `__` (e.g. `__zero_0`, `__if_1_true`).
- User-defined labels must **not** begin with `__`.
- Duplicate label names are a parse-time error.

---

## 3. Instructions

```ebnf
instruction  = add-imm
             | add-reg
             | negate
             | branch
             | goto-instr
             | input-instr
             | output-instr
             | raw-moves ;
```

### 3.1 `add` — Add Immediate

```ebnf
add-imm      = "add" " " register " " "#" integer ;
integer      = digit { digit } ;
```

**Example:** `add R1 #3`

**Constraints:**
- The immediate `m` must satisfy `0 ≤ m < Kᵢ` (where `Kᵢ` is the order of register `Rᵢ`).
- This check is deferred to **run start**, after the register manifest is loaded (Task 4.8).
- Negative immediates never appear; subtraction is encoded as `m = (Kᵢ − n) mod Kᵢ` by the
  compiler.

### 3.2 `add` — Add Register

```ebnf
add-reg      = "add" " " register " " register ;
```

**Example:** `add R1 R2`

- Adds the current value of the source register into the destination register modulo `Kᵢ`.

### 3.3 `negate`

```ebnf
negate       = "negate" " " register ;
```

**Example:** `negate R2`

### 3.4 `branch`

```ebnf
branch       = "branch" " " "cycle(" piece-list ")" " " identifier ;
piece-list   = piece-label { "," piece-label }
             | (* empty — checks all 48 facelets *) ;
piece-label  = "UF" | "UFL" | "UR" | "UFR" | "UB" | "UBR" | "UL" | "UBL"
             | "DF" | "DFL" | "DR" | "DFR" | "DB" | "DBR" | "DL" | "DBL"
             | "FL" | "FR" | "BL" | "BR" ;
```

**Example:** `branch cycle(UF,UFL,UR) __loop_done_0`

**Rules:**
- Piece labels are **comma-separated with no spaces**.
- Register conditions are lowered by codegen to concrete piece lists; assembly does **not**
  accept `cycle(R1)` or any other register name inside `cycle(...)`.
- An empty piece list `cycle()` branches if all 48 non-centre facelets are home (i.e. the cube
  is fully solved).
- The target must be a label defined somewhere in the program (checked during the
  label→index pre-pass).

### 3.5 `goto`

```ebnf
goto-instr   = "goto" " " identifier ;
```

**Example:** `goto __loop_0`

### 3.6 `input`

```ebnf
input-instr  = "input" " " string-literal ;
string-literal = '"' { any-char } '"' ;
any-char     = (* any character except '"' and newline *) ;
```

**Example:** `input "Enter a move sequence: "`

### 3.7 `output`

```ebnf
output-instr = "output" [ " " string-literal ] ;
```

**Examples:**
- `output` — outputs the R0 count only.
- `output "result: "` — outputs the label string followed by the R0 count.

### 3.8 Raw Moves

A raw-moves line contains a simplified Rubik's cube move sequence. It is emitted by the
compiler for `apply` expressions (Task 4.6) after running `alg_simplify`.

```ebnf
raw-moves    = move { " " move } ;
move         = face-letter [ modifier ] ;
face-letter  = "U" | "D" | "L" | "R" | "F" | "B" ;
modifier     = "'" | "2" ;
```

**Examples:** `R U R' U'`, `U2`, `F2 R U R' U' F2`

**Rules:**
- Moves are **single-space separated**.
- A raw-moves line that simplifies to the empty sequence is **not emitted** (Task 4.6).

---

## 4. Register Manifest Header

The manifest is emitted by `codegen.c` as `;`-prefixed comment lines **before** the first
numbered instruction line. The JS parser reads these to populate the register table.

```ebnf
manifest-line = ";" " " "reg" " " reg-index " " "alg=" quoted-alg " " "order=" integer newline ;
reg-index     = digit { digit } ;
quoted-alg    = '"' move { " " move } '"' ;
```

**Example:**
```
; reg 0 alg="U" order=4
; reg 1 alg="R U R' U'" order=6
```

**Rules:**
- Register 0 (R0) is always present and is fixed by the R0 contract (`alg="U"`, `order=4`).
- Manifest lines must appear before any numbered instruction line.
- The JS parser derives the cycle set for each register from its `alg` string using
  `cyclesetFromAlg` (Task 4.9) — it is **not** stored in the manifest.

---

## 5. Register Reference

```ebnf
register     = "R" digit { digit } ;
```

**Examples:** `R0`, `R1`, `R12`

- `R0` is the I/O register (fixed; algorithm = `U`, order = 4).
- All other registers are assigned by the register allocator.

---

## 6. Complete Grammar (Consolidated)

```ebnf
(* CuBit Assembly — complete EBNF  (Task 0.4 / Task 4.7) *)

program        = { manifest-line } { line } EOF ;

manifest-line  = ";" " " "reg" " " reg-index " " "alg=" quoted-alg
                 " " "order=" integer newline ;
reg-index      = digit { digit } ;
quoted-alg     = '"' move { " " move } '"' ;

line           = line-number "  " ( instruction | label-def ) newline ;
line-number    = digit { digit } ;

label-def      = identifier ":" ;

instruction    = add-imm
               | add-reg
               | negate
               | branch
               | goto-instr
               | input-instr
               | output-instr
               | raw-moves ;

add-imm        = "add" " " register " " "#" integer ;
add-reg        = "add" " " register " " register ;
negate         = "negate" " " register ;
branch         = "branch" " " "cycle(" piece-list ")" " " identifier ;
goto-instr     = "goto" " " identifier ;
input-instr    = "input" " " string-literal ;
output-instr   = "output" [ " " string-literal ] ;
raw-moves      = move { " " move } ;

piece-list     = [ piece-label { "," piece-label } ] ;
piece-label    = "UF" | "UFL" | "UR" | "UFR" | "UB" | "UBR" | "UL" | "UBL"
               | "DF" | "DFL" | "DR" | "DFR" | "DB" | "DBR" | "DL" | "DBL"
               | "FL" | "FR" | "BL" | "BR" ;

register       = "R" digit { digit } ;
integer        = digit { digit } ;
identifier     = letter { letter | digit | "_" } ;
string-literal = '"' { any-char } '"' ;
move           = face-letter [ modifier ] ;
face-letter    = "U" | "D" | "L" | "R" | "F" | "B" ;
modifier       = "'" | "2" ;
letter         = "a"…"z" | "A"…"Z" | "_" ;
digit          = "0"…"9" ;
any-char       = (* any character except '"' and newline *) ;
newline        = "\n" | "\r\n" ;
```

---

## 7. Example Assembly File

```
; reg 0 alg="U" order=4
; reg 1 alg="R U R' U'" order=6
10  __zero_0:
20  branch cycle(UF,UFL,UR,UFR,UB,UBR,UL,UBL) __zero_done_0
30  add R0 #1
40  goto __zero_0
50  __zero_done_0:
60  add R1 #3
70  __loop_0:
80  branch cycle(UF,UFL,UR,UFR,UB,UBR,UL,UBL) __loop_done_0
90  add R1 #1
100  goto __loop_0
110  __loop_done_0:
120  output "count: "
```

---

## 8. Parser Behaviour

The JS `parse_assembly(text)` function must:

1. **Strip and skip** blank lines and `;`-prefixed manifest/comment lines.
2. **Validate line numbers** are strictly increasing in steps of 10.
3. **Perform a label→index pre-pass** before executing any instruction, building a
   `Map<string, number>` from label name to instruction array index.
4. **Reject malformed lines** with `console.error` and refuse to run.
5. **Defer range checks** (`0 ≤ m < Kᵢ`) to run start, once the register manifest is loaded.
6. **Produce instruction objects** per v1.0 §9.2.3, e.g.:
   ```js
   { op: "add_imm", reg: 1, imm: 3 }
   { op: "add_reg", dst: 1, src: 2 }
   { op: "branch",  pieces: ["UF","UFL"], target: "__loop_done_0" }
   { op: "goto",    target: "__loop_0" }
   { op: "raw_moves", seq: "R U R' U'" }
   ```
