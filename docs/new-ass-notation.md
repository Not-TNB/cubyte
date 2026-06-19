# CuBit Assembly — Grammar (derived from `codegen.c`)

Corrects `assembler-notation-ebnf.md` against the actual emitter. Two real differences:

- **Line numbers increment by 1 starting at 1** (`codegen.c` `line_counter = 1`, `emit()` does `counter++`), not steps of 10.
- **`add`/`negate` mnemonics do not exist.** All arithmetic is lowered to raw move algorithms before emission (`emit_add_constant`, `emit_zero`, etc. all bottom out in `emit_alg_if_nonempty`/`entry->algorithm`). The only instruction forms `emit()` ever produces are: label, `branch`, `goto`, `input`, `output`, raw-moves.
- Piece labels denote the 20 cubies (8 corners + 12 edges), not facelets.

```ebnf
program        = { manifest-line } { line } EOF ;

manifest-line  = ";" " " "reg" " " reg-index " " "alg=" quoted-alg
                 " " "order=" integer "\n" ;
reg-index      = digit { digit } ;
quoted-alg     = '"' [ move { " " move } ] '"' ;

line           = line-number "  " instruction "\n" ;
line-number    = digit { digit } ;            (* = previous line-number + 1, first line = 1 *)

instruction    = label-def
               | branch
               | goto-instr
               | input-instr
               | output-instr
               | raw-moves ;

label-def      = identifier ":" ;
branch         = "branch" " " "cycle(" piece-list ")" " " identifier ;
goto-instr     = "goto" " " identifier ;
input-instr    = "input" " " string-literal ;
output-instr   = "output" [ " " string-literal ] ;
raw-moves      = move { " " move } ;          (* never emitted if the alg simplifies to empty *)

piece-list     = [ piece-label { "," piece-label } ] ;   (* empty = whole-cube-solved test *)
piece-label    = "UFL" | "UF" | "UFR" | "UL" | "UR" | "UBL" | "UB" | "UBR"
               | "DFL" | "DF" | "DFR" | "DL" | "DR" | "DBL" | "DB" | "DBR"
               | "FL"  | "FR" | "BL"  | "BR" ;

move           = face-letter [ modifier ] ;
face-letter    = "U" | "D" | "L" | "R" | "F" | "B" ;
modifier       = "'" | "2" ;

identifier     = letter { letter | digit | "_" } ;
letter         = "a"…"z" | "A"…"Z" | "_" ;
digit          = "0"…"9" ;
integer        = digit { digit } ;
string-literal = '"' { any-char } '"' ;
any-char       = (* any character except '"' or newline *) ;
```

## Notes for the parser

- Manifest lines (`; reg ...`) precede all numbered lines and are skipped/consumed separately.
- Compiler-generated labels start with `__` (e.g. `__zero_0`); reject duplicates.
- `branch`/`goto` targets must resolve in a label→index pre-pass before execution.
- Comma-separated piece lists have **no spaces**; moves are **single-space** separated.
- `output` with no string only reports the `R0` count; with a string it prefixes that label.
