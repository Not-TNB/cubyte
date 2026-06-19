CuBit codegen implementation plan
=================================

Goal
----

Implement the final compiler pass:

    desugared ProgramAST + TypeEnv + RegTable + colouring -> numbered .asm file

Codegen does not parse source, typecheck expressions, compute liveness, build
the interference graph, or choose registers. Earlier passes own those jobs.
Codegen walks the already-desugared AST and emits physical CuBit assembly:

    - numbered raw move lines
    - labels
    - branch cycle(...) label
    - goto label
    - input/output

There are no assembly pseudo-instructions such as:

    add R2 #3
    add R2 R3
    negate R3

Those are not the target machine. To increment a register, emit that register's
cube algorithm as a raw move line. To decrement a register, emit the inverse
algorithm. Larger arithmetic is built from those operations and branches.

Expected pipeline position:

    lexer -> parser -> typechecker -> desugarer
          -> cfg_build -> liveness_analyze -> ig_build
          -> regalloc_run -> codegen -> write .asm


1. Register contract
--------------------

The first two physical registers are reserved forever:

    R0
        Input/output register.
        input writes it.
        output reads/drains it.
        User variables must never be allocated to R0.

    R1
        Scratch/temp register used internally by codegen.
        User variables must never be allocated to R1.

User int variables must be assigned physical registers starting at:

    R2, R3, ...

Codegen should validate this contract:

    - regs->count >= 2
    - colouring[index] for a user int variable is >= 2
    - R0 is only used for input/output
    - R1 is only used by codegen helper sequences

If regalloc currently only reserves R0, update regalloc so it also creates and
reserves R1 before allocating user variables.


2. Public API
-------------

Create/maintain:

    extension/include/codegen.h
    extension/src/codegen.c

The header should expose one boring entry point:

    #ifndef ARMV8_37_EXTENSION_CODEGEN_H
    #define ARMV8_37_EXTENSION_CODEGEN_H

    #include "program_ast.h"
    #include "regalloc.h"
    #include "typechecker.h"

    typedef struct CodeGen CodeGen;

    void codegen_program(FILE *out, ProgramAST *program, TypeEnv *env,
                         RegTable *regs, const int *colouring);

    #endif

Most helpers stay static in codegen.c.


3. Internal CodeGen state
-------------------------

Use:

    #define IO_REGISTER_INDEX 0
    #define TEMP_REGISTER_INDEX 1

    struct CodeGen {
        FILE *out;
        ProgramAST *program;
        TypeEnv *env;
        RegTable *regs;
        const int *colouring;

        int line_counter;
        int label_counter;

        bool *written;
        bool past_first_label;
    };

Field meanings:

    out
        Destination .asm file.

    env
        Name/type/index table from typechecker.

    regs
        Physical cube registers. Each RegEntry provides:
            algorithm string A_i
            order K_i
            cycle set C_i
            physical register index i

    colouring
        Array indexed by TypeEnv variable index.
        For user int variables, colouring[i] is a RegTable index >= 2.
        For alg vars, use -1.
        R0 may be precoloured as 0 internally, but user vars never use it.

    line_counter
        Starts at 1. Every numbered assembly line uses current line_counter,
        then increments by 1.

    label_counter
        Monotonic counter for compiler-generated labels.

    written
        bool array sized regs->count. Tracks whether a physical register has
        already been written by emitted code.

    past_first_label
        Becomes true after codegen emits any label. Used by the
        virgin-register optimisation.


4. Basic helpers
----------------

4.1 codegen_init / codegen_free

Initialise:

    line_counter = 1
    label_counter = 0
    written = calloc(regs->count, sizeof(bool))
    past_first_label = false

Check all required pointers before dereferencing them. On internal misuse, call:

    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, ...)

4.2 emit

Write exactly:

    <line number><two spaces><instruction>\n

Example:

    1  R U R' U'
    2  branch cycle(UF,UR) __done_0
    3  goto __loop_1

Use a varargs helper:

    static void emit(CodeGen *cg, const char *fmt, ...);

Rules:

    - line numbers strictly increase by 1
    - exactly two spaces after the line number
    - caller format strings should not include a trailing newline
    - emit appends the newline itself

4.3 emit_label

Labels are numbered lines:

    12  __while_cond_0:

emit_label should set:

    cg->past_first_label = true;

4.4 fresh_label

Compiler labels begin with "__":

    __zero_0
    __while_cond_1
    __if_true_2

Return a heap string owned by the caller:

    static char *fresh_label(CodeGen *cg, const char *prefix);

4.5 var_index / reg_for_var

Map source variable name to physical register:

    name -> TypeEnv index -> colouring[index] -> RegTable entry

Rules:

    - missing variable is an internal error
    - alg variables must not be passed to reg_for_var
    - user int variables must map to registers >= 2
    - R0 and R1 are not valid user variable registers

4.6 mod_reduce

Use modular reduction before realising powers:

    static int mod_reduce(int n, int k) {
        int r = n % k;
        return r < 0 ? r + k : r;
    }


5. Assembly output model
------------------------

The only arithmetic-looking operation codegen may emit is a raw move sequence.

To increment register Ri by 1:

    emit raw moves for A_i

To decrement register Ri by 1:

    parse A_i with alg_parse
    invert with alg_invert
    emit alg_to_string(inverse)

To add an integer n to Ri:

    m = mod_reduce(n, K_i)
    parse A_i with alg_parse
    alg_power_realise(A_i, m, K_i, &out)
    emit alg_to_string(out), unless empty

To subtract an integer n from Ri:

    m = mod_reduce(n, K_i)
    parse A_i with alg_parse
    invert A_i with alg_invert
    alg_power_realise(A_i_inverse, m, K_i, &out)
    emit alg_to_string(out), unless empty

Use alg.c for parsing, inversion, power realisation, and simplification. Do not
manually assemble repeated strings when alg.c can do it correctly.

Suggested helpers:

    static void emit_add_constant(CodeGen *cg, int reg_index, int imm_val);
    static void emit_sub_constant(CodeGen *cg, int reg_index, int imm_val);

Use the helper names already present in codegen.c. Do not introduce a separate
"delta" helper unless the existing add/sub split becomes genuinely painful.


6. Register manifest
--------------------

If the emulator still consumes a register manifest, emit it before the first
numbered instruction:

    ; reg 0 alg="U" order=4
    ; reg 1 alg="..." order=...
    ; reg 2 alg="..." order=...

Helper:

    static void emit_register_info(CodeGen *cg);

These comment lines are not numbered.


7. CycleSet to branch piece list
--------------------------------

Branch syntax uses concrete piece names:

    branch cycle(UF,UFR,UR) __label

It does not accept register names:

    branch cycle(R2) __label    // wrong

Implement:

    static char *cycle_list(CodeGen *cg, int reg_index);

It should inspect:

    cg->regs->regs[reg_index].cycles

and return a heap string containing comma-separated piece labels with no
spaces, using piece_to_string.

For solved[] with no explicit pieces, emit:

    branch cycle() __label


8. Zeroing a register
---------------------

There is no direct "set register to zero". A register is zero when the pieces
in its cycle set are solved. To clear Ri:

    __zero_0:
    branch cycle(<Ri pieces>) __zero_done_1
    <A_i>
    goto __zero_0
    __zero_done_1:

Numbered example:

    1  __zero_0:
    2  branch cycle(UF,UR) __zero_done_1
    3  R U R' U'
    4  goto __zero_0
    5  __zero_done_1:

Suggested helper:

    static void emit_zero(CodeGen *cg, int reg_index);

Details:

    - use fresh labels
    - use cycle_list(reg_index)
    - emit the register algorithm as raw moves
    - mark written[reg_index] = true
    - labels set past_first_label


9. Virgin-register optimisation
-------------------------------

A physical register starts at zero because the cube starts solved. Codegen may
skip zeroing on an initial write iff:

    !cg->written[reg_index] && !cg->past_first_label

Use this only for let initialisation or first assignment-style writes where the
register is genuinely being initialised. This matters because:

    - regalloc may reuse a physical register for a different variable
    - a backward goto/while may re-execute a let

After any emitted write:

    cg->written[reg_index] = true;


10. Primitive assignment forms
------------------------------

The desugarer should reduce assignments to primitive forms before codegen.
Codegen should be strict. If a complex expression reaches this pass, die with:

    "non-primitive assignment reached codegen"

Supported int forms that should be routed through the existing helpers:

    1. let x : int := n
    2. x := n
    3. x := y
    4. x := x + n
    5. x := x - n
    6. x := x + y
    7. x := x - y

10.1 let x : int := n / let x : int := 0 after desugar

Use the existing:

    emit_let_statement(CodeGen *codegen, char *var_name)

Current desugarer comments say int lets become:

    let x : int := 0

with the real value emitted later as an assign statement. Under that convention,
emit_let_statement only needs to initialise/zero the allocated register:

    Rx = register_for_var(x)
    emit_zero(Rx)
    written[Rx] = true

If the desugarer instead leaves a nonzero int literal in a let, handle it with:

    emit_add_constant(Rx, n)

after the zero/init step.

10.2 x := n

Use the existing:

    emit_assign_constant(CodeGen *codegen, int reg_index, int imm_val)

Its shape is:

    emit_zero(Rx)
    emit_add_constant(Rx, n)
    written[Rx] = true

10.3 x := y

Use the existing:

    emit_assign_variable(CodeGen *codegen, int res_reg_index, int val_reg_index)

Its shape is:

    emit_zero(Rx)
    emit_add_variable(Rx, Ry)

emit_add_variable must preserve y.

10.4 x := x + n

Emit:

    emit_add_constant(Rx, n)

10.5 x := x - n

Emit:

    emit_sub_constant(Rx, n)

10.6 x := x + y

Use the existing:

    emit_add_variable(CodeGen *codegen, int res_reg_index, int val_reg_index)

It uses R1 as temp and preserves y:

    emit_zero(R1)

    while y != 0:
        y--
        x++
        R1++

    while R1 != 0:
        R1--
        y++

Assembly skeleton:

    __add_load_0:
    branch cycle(<Ry pieces>) __add_restore_1
    <A_y inverse>
    <A_x>
    <A_1>
    goto __add_load_0

    __add_restore_1:
    branch cycle(<R1 pieces>) __add_done_2
    <A_1 inverse>
    <A_y>
    goto __add_restore_1

    __add_done_2:

10.7 x := x - y

Use the existing:

    emit_sub_variable(CodeGen *codegen, int res_reg_index, int val_reg_index)

It uses R1 as temp and preserves y:

    emit_zero(R1)

    while y != 0:
        y--
        x--
        R1++

    while R1 != 0:
        R1--
        y++

This is the same as add-variable except x uses the inverse algorithm.

Important alias rules:

    - Rx and Ry may be the same only for forms that make sense after desugaring.
    - R1 is reserved. Do not compile user variables into R1.
    - If a helper would need R1 while operating on R1 as a user variable, that is
      a compiler bug because user variables cannot be assigned R1.


11. Conditions
--------------

Use the existing condition entry point:

    static void emit_condition(CodeGen *codegen, Cond *cond);

For if/while lowering, use helpers that accept the labels they need. codegen.c
already has these stubs:

    static void emit_if_variable_cond(CodeGen *codegen, int reg_index,
                                      int val_index,
                                      char *cond_true_label,
                                      char *cond_false_label);
    static void emit_if_solved_pieces_cond(CodeGen *codegen, Statement *if_stmt);
    static void emit_while_constant_cond(CodeGen *codegen, int reg_index,
                                         int N,
                                         char *body_label);

Fill those out rather than renaming the condition layer.

11.1 not condition

Compile by swapping labels:

    emit the inner condition with true/false labels swapped

11.2 solved[pieces]

Emit:

    branch cycle(<pieces>) ltrue
    goto lfalse

If the list is empty:

    branch cycle() ltrue
    goto lfalse

11.3 x = n

To test x == n, temporarily shift x by -n. If its cycle pieces are solved, x
was n. Restore x on both paths.

Pattern:

    emit_sub_constant(Rx, n)
    branch cycle(<Rx pieces>) Ltrue_stub
    emit_add_constant(Rx, n)
    goto Lfalse
    Ltrue_stub:
    emit_add_constant(Rx, n)
    goto Ltrue

Use a fresh true stub label.

11.4 x = y

Use the existing condition helper:

    emit_if_variable_cond(codegen, reg_index, val_index,
                          cond_true_label, cond_false_label)

Important: the current code calls emit_assign_variable with R1 as the
destination, but emit_assign_variable calls emit_add_variable, and
emit_add_variable itself uses R1 as temp. Fix that implementation before relying
on x = y conditions.

One physical single-temp pattern is:

    emit_zero(R1)

    while x != 0:
        x--
        y--
        R1++

    branch cycle(<Ry pieces>) Ltrue_stub

    restore x/y from R1
    goto Lfalse

    Ltrue_stub:
    restore x/y from R1
    goto Ltrue

The restore loop is:

    while R1 != 0:
        R1--
        x++
        y++

This preserves x and y and leaves R1 solved on both paths. It compares whether
y - x is zero in y's register. If the language requires stronger equality
semantics across registers with different orders, resolve that at the
desugarer/regalloc boundary before implementing this condition.

If program_ast.h/desugarer currently carry condition temp fields, decide whether
they are still needed. With fixed physical R1, codegen likely does not need
named condition temps.


12. If and while
----------------

12.1 if

For:

    if cond { then_body } else { else_body }

Use the existing:

    static void compile_if(CodeGen *codegen, Statement *if_stmt)

Its target structure should be:

    Ltrue = fresh_label("if_true")
    Lfalse = fresh_label("if_false")
    Lend = fresh_label("if_end")

    emit/compile condition using Ltrue and Lfalse
    Ltrue:
        compile then_body
        goto Lend
    Lfalse:
        compile else_body
    Lend:

Keep the false and end labels even when there is no else body.

12.2 while

For:

    while cond { body }

Generate:

    Lcond = fresh_label("while_cond")
    Lbody = fresh_label("while_body")
    Lend = fresh_label("while_end")

    Lcond:
        emit/compile condition using Lbody and Lend
    Lbody:
        compile body
        goto Lcond
    Lend:

The first emitted label disables virgin zero skipping for the rest of the
program, which is the safe behaviour around loops and gotos.


13. Labels and goto
-------------------

User labels are already validated by typechecker.

STMT_LABEL:

    emit_label(cg, stmt->label.name)

STMT_GOTO:

    emit(cg, "goto %s", stmt->goto_.target)

User labels should not begin with "__"; parser/typechecker should enforce this.
Codegen simply emits the labels it receives.


14. input, output, apply
------------------------

14.1 input

Source:

    input "prompt"

Assembly:

    input "prompt"

This writes R0 at runtime.

14.2 output

Source:

    output
    output "label"

Assembly:

    output
    output "label"

This reads/drains R0 at runtime.

14.3 apply

Source:

    apply <alg expr>

Codegen should:

    1. Resolve alg variables from TypeEnv entries.
    2. Concatenate alg strings.
    3. Parse with alg_parse.
    4. Simplify with alg_simplify.
    5. Emit one raw-moves line using alg_to_string.
    6. If the simplified algorithm is empty, emit nothing.

No "apply" keyword appears in assembly.


15. Program walk
----------------

codegen_program still needs a top-level statement walk. Add the smallest
dispatcher glue needed, but route to the helper names already present in
codegen.c:

    STMT_LET       -> emit_let_statement
    STMT_ASSIGN    -> emit_assign_constant / emit_assign_variable /
                       emit_add_constant / emit_sub_constant /
                       emit_add_variable / emit_sub_variable
    STMT_IF        -> compile_if
    STMT_WHILE     -> use emit_while_constant_cond and the same label pattern
    STMT_GOTO      -> emit_goto
    STMT_LABEL     -> emit_label
    STMT_INPUT     -> emit input with emit
    STMT_OUTPUT    -> emit output with emit
    STMT_APPLY     -> emit raw moves with emit

Top-level codegen_program:

    validate args
    init CodeGen
    emit register info if required
    compile_statements(program->statements, program->count)
    free CodeGen internals


16. Testing strategy
--------------------

Add:

    extension/tests/test_codegen.c

Keep tests AST-based at first.

Recommended tests:

16.1 Line numbering

Compile two simple emitted lines and assert:

    1  ...
    2  ...

16.2 Reserved registers

Assert user variables are never accepted in R0 or R1.

16.3 Virgin let

Program:

    let x : int := 3

Expected:

    no zero loop before the first write
    raw moves for A_x^3, simplified through alg_power_realise

16.4 Reassignment constant

Program:

    let x : int := 3
    x := 2

Expected:

    first write may skip zero
    second write emits a zero loop before A_x^2

16.5 Negative lowering

Program:

    x := x - 1

Expected:

    emits the inverse/power-realised form of A_x^-1, not a negative immediate

16.6 Copy

Program:

    x := y

Expected:

    zero x
    loops over y using R1
    y is restored

16.7 Add/sub variable

Programs:

    x := x + y
    x := x - y

Expected:

    y is restored
    R1 ends solved after the helper

16.8 if x = n restore

Assert both true and false paths restore x before jumping away.

16.9 while labels

Assert output contains:

    __while_cond
    __while_body
    __while_end
    goto __while_cond

16.10 apply simplification

Program:

    apply "R R'"

Expected:

    no raw-moves line emitted


17. Important gotchas
---------------------

17.1 Do not emit add/negate pseudo-instructions.

The assembly is raw moves plus control/input/output only.

17.2 Do not emit register names inside cycle(...).

Wrong:

    branch cycle(R2) label

Right:

    branch cycle(UF,UFR,UR) label

17.3 Do not use R0 or R1 for user variables.

R0 is input/output.
R1 is codegen temp.
User variables start at R2.

17.4 Do not skip zeroing just because a statement is a let.

The virgin optimisation depends on the physical register, not the variable
name. Regalloc can reuse registers, and labels/gotos can re-enter code.

17.5 Do not simplify across instruction boundaries.

Each numbered line is an observable branch boundary. It is okay to simplify the
raw moves for one emitted add/sub constant or one apply expression. Do not merge
adjacent emitted lines together.

17.6 Free heap strings and Alg storage.

cycle_list, fresh_label, alg_to_string, and Alg values all have ownership that
must be cleaned up. ASan should stay quiet.


18. Suggested implementation order
----------------------------------

Build and test after each chunk.

1. Fix codegen.h include guard and remove self-include.
2. Ensure regalloc reserves R0 and R1, with user variables from R2.
3. Implement CodeGen init/free and validation.
4. Implement emit, emit_label, fresh_label with line numbers starting at 1.
5. Implement reg_for_var with R0/R1 rejection for user vars.
6. Implement cycle_list.
7. Fix emit_add_constant and emit_sub_constant using alg.c.
8. Implement/fix emit_zero.
9. Implement emit_let_statement and emit_assign_constant/variable.
10. Finish emit_add_variable and emit_sub_variable using R1.
11. Implement labels and goto.
12. Implement solved[], not, x = n, x = y conditions.
13. Implement if.
14. Implement while.
15. Implement input/output.
16. Implement apply lowering.
17. Wire codegen into main.c.
18. Add golden-output tests for full small programs.

t  = t + x_pre

t = t_pre
tt = 0
x = x_pre

t = t_pre
tt = x_pre
x = 0

t = t_pre + x_pre
tt = 0
x = x_pre


x = y 

t = x
zero(t)
t += x






19. Minimal milestone
---------------------

The first useful milestone is:

    let x : int := 3;
    let y : int := x;
    x := x + 2;

It should emit valid numbered physical assembly:

    - comments/manifest if required
    - line numbers 1, 2, 3, ...
    - raw moves, not add/negate instructions
    - zeroing loops when needed
    - R1 used as temp for copying y
    - no user variable mapped to R0 or R1

After that, add branches and loops.
