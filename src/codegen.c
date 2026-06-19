#include "../include/codegen.h"
#include "../include/util.h"
#include "../include/alg.h"
#include "../include/program_ast.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define IO_REGISTER_INDEX 0
#define TEMP_REGISTER_INDEX 1
#define LABEL_BUFFER_SIZE 128
#define CYCLE_LIST_BUFFER_SIZE (PC_COUNT * 4 + 1)

#define die_internal_null die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "null pointer exception during codegen");

static noreturn void die_internal_error(const char *msg) {
    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, msg);
}

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

static void emit_add_constant(CodeGen *codegen, int reg_index, int imm_val);
static void emit_sub_constant(CodeGen *codegen, int reg_index, int imm_val);
static void emit_add_variable(CodeGen *codegen, int res_reg_index, int val_reg_index);
static void emit_sub_variable(CodeGen *codegen, int res_reg_index, int val_reg_index);
static void compile_statement(CodeGen *codegen, Statement *statement);
static void compile_statements(CodeGen *codegen, Statement **statements, int count);

// ----------------------- CREATING AND DELETING --------------

CodeGen *codegen_init( FILE *out, ProgramAST *ast, TypeEnv *env, RegTable *regs, const int *colouring ) {
    CodeGen *codegen = malloc(sizeof(CodeGen));
    if (codegen == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "memory exceeded, malloc failed when allocating Codegen");
    }

    *codegen = (CodeGen) {
        .out = out,
        .program = ast,
        .env = env,
        .regs = regs,
        .colouring = colouring,
        .line_counter = 1, // line number in assembly 
        .label_counter = 0,
        .written = calloc(regs->count, sizeof(bool)),
        .past_first_label = false,
    };

    if (codegen->written == NULL) {
        free(codegen);
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "memory exceeded, calloc failed when allocating Codegen");
    }

    return codegen;
}

void codegen_free(CodeGen *codegen) {
    if (codegen == NULL) {
        return;
    }
    free(codegen->written);
    free(codegen);
}

// ----------------------- HELPERS --------------

// adds one line to the output file. increments line counter
static void emit(CodeGen *codegen, const char *format, ...) {
    va_list args;

    fprintf(codegen->out, "%d  ", codegen->line_counter);
    codegen->line_counter++;

    // initialises args pointer by telling it that it comes after format
    va_start(args, format);
    vfprintf(codegen->out, format, args);
    // necessary cleanup
    va_end(args);

    fputc('\n', codegen->out);
}

// writes a label to the output file. Labels are on new lines
static void emit_label(CodeGen *codegen, const char *label) {
    emit(codegen, "%s:", label);
    
    // after we're past first label, we cannot take any chances
    // and must zero every register before using. 
    codegen->past_first_label = true;
}

// writes a unique label into caller-owned storage
static void fresh_label(CodeGen *codegen, char *out, size_t out_size,
                        const char *prefix) {
    int label_id = codegen->label_counter++;
    int written = snprintf(out, out_size, "__%s_%d", prefix, label_id);

    if (written < 0 || (size_t)written >= out_size) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
            "failed to format label, ensure label buffer is large enough");
    }
}

// returns the index corresponding to the variable 'name' in typeenv
static int var_index(CodeGen *codegen, const char *name) {
    if (name == NULL || codegen == NULL) {
        die_internal_null;
    }

    int index_of_var = typeenv_index(codegen->env, name);

    if (index_of_var == -1) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "variable %s missing during codegen", name);
    }

    return index_of_var;
}

// returns the index of the register corresponding to variable 'name'
static int register_for_var(CodeGen *codegen, const char *name) {
    int index = var_index(codegen, name);

    VarEntry *entry = typeenv_lookup(codegen->env, name);
    if (entry == NULL || entry->type != TYPE_INT) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "register not found, or is not an integer");
    }

    int reg = codegen->colouring[index];
    if (reg < 0 || reg >= codegen->regs->count) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "register not found");
    }

    if (reg == TEMP_REGISTER_INDEX) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
            "user variable '%s' was assigned reserved temp register R1", name);
    }

    if (reg == IO_REGISTER_INDEX && codegen->regs->r0_reserved &&
        strcmp(name, R0_SYNTHETIC_VARIABLE) != 0) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
            "user variable '%s' was assigned reserved I/O register R0", name);
    }

    return reg;
}

static void ensure_codegen_register_contract(RegTable *regs) {
    if (regs->count <= TEMP_REGISTER_INDEX) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
            "codegen requires R0 and reserved temp R1");
    }
}

static void require_temp_can_hold(CodeGen *codegen, int reg_index, const char *context) {
    int temp_order = codegen->regs->regs[TEMP_REGISTER_INDEX].order;
    int reg_order = codegen->regs->regs[reg_index].order;

    if (temp_order < reg_order) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
            "%s requires temp order >= source order, got temp K=%d and R%d K=%d",
            context, temp_order, reg_index, reg_order);
    }
}

static int mod_reduce(int n, int k) {
    int r = n % k;
    return r < 0 ? r + k : r;
}

// Parse the raw algorithm string stored in a register table entry into Alg form.
// Codegen uses this whenever it needs to invert or realise powers of a register.
static void parse_register_algorithm(CodeGen *codegen, int reg_index, Alg *out) {
    if (!alg_parse(codegen->regs->regs[reg_index].algorithm, out)) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
            "failed to parse register algorithm during codegen");
    }
}

// Emit one numbered raw-move line for alg, unless alg simplifies to no moves.
// Empty move sequences are valid internally but should not produce blank asm.
static void emit_alg_if_nonempty(CodeGen *codegen, const Alg *alg) {
    char *alg_string = alg_to_string(alg);
    if (alg_string[0] != '\0') {
        emit(codegen, "%s", alg_string);
    }
    free(alg_string);
}

static void emit_register_info(CodeGen *codegen) {
    for (int i = 0; i < codegen->regs->count; i++) {
        RegEntry *entry = &codegen->regs->regs[i];
        fprintf(codegen->out, "; reg %d alg=\"%s\" order=%d\n", entry->index, entry->algorithm, entry->order);
    }
}

static char *register_inverse_algorithm_string(CodeGen *codegen, int reg_index) {
    Alg alg = {0};
    Alg inverse = {0};
    parse_register_algorithm(codegen, reg_index, &alg);
    alg_invert(&alg, &inverse);
    alg_simplify(&inverse);

    char *inverse_string = alg_to_string(&inverse);
    alg_free(&inverse);
    alg_free(&alg);
    return inverse_string;
}

static void cycle_list(CodeGen *codegen, const int reg_index, char *out,
                       size_t out_size) {
    CycleSet cycles = codegen->regs->regs[reg_index].cycles;
    if (out_size == 0) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "empty cycle list buffer");
    }

    out[0] = '\0';
    size_t used = 0;
    bool first = true;
    for (int i = 0; i < PC_COUNT; i++) {
        if ((cycles & (1u << i)) == 0) {
            continue;
        }

        const char *piece_name = piece_to_string(i);
        if (piece_name == NULL) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
                "invalid piece index in cycle list");
        }

        int written = snprintf(out + used, out_size - used, "%s%s",
                               first ? "" : ",", piece_name);
        if (written < 0 || (size_t)written >= out_size - used) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1,
                "cycle list buffer too small");
        }
        used += (size_t)written;
        first = false;
    }
}

// emits: goto label
static void emit_goto(CodeGen *codegen, const char *label) {
    emit(codegen, "goto %s", label);
}

static void emit_branch(CodeGen *codegen, const char *cycle, const char *label) {
    emit(codegen, "branch cycle(%s) %s", cycle, label);
}

/*
emits the instructions for zeroing the reg_index register. Example:

__zero_0:
branch cycle(<register pieces>) __zero_done_1
F U 
goto __zero_0
__zero_done_1:

*/
static void emit_zero(CodeGen *codegen, const int reg_index) {
    if (codegen == NULL || reg_index < 0 || reg_index >= codegen->regs->count) {
        die_internal_error("emitting zero failed");
    } 

    // zero register optimisation - can skip zeroing when we've never written
    // to it or if we've never gone past a label (no chance of looping back)
    if (!codegen->written[reg_index] && !codegen->past_first_label) {
        return;
    }

    RegEntry *entry = &codegen->regs->regs[reg_index];
    char zero_label[LABEL_BUFFER_SIZE];
    char done_label[LABEL_BUFFER_SIZE];
    char cycles[CYCLE_LIST_BUFFER_SIZE];
    fresh_label(codegen, zero_label, sizeof zero_label, "zero");
    fresh_label(codegen, done_label, sizeof done_label, "zero_done");
    cycle_list(codegen, reg_index, cycles, sizeof cycles);

    emit_label(codegen, zero_label);
    emit_branch(codegen, cycles, done_label);
    emit(codegen, "%s", entry->algorithm);
    emit_goto(codegen, zero_label);    
    emit_label(codegen, done_label);

    codegen->written[reg_index] = true;

}


// int argument for temp register as well (no name)
// output is 'algorithm x imm_val' e.g. (R U R' U') x 10
static void emit_add_constant(CodeGen *codegen, int reg_index, int imm_val) {
    int order = codegen->regs->regs[reg_index].order;
    int amount = mod_reduce(imm_val, order);
    if (amount == 0) {
        return;
    }

    Alg alg = {0};
    Alg realised = {0};
    parse_register_algorithm(codegen, reg_index, &alg);
    alg_power_realise(&alg, amount, order, &realised);
    emit_alg_if_nonempty(codegen, &realised);
    alg_free(&realised);
    alg_free(&alg);

    // no need to update written: guaranteed to have assigned or let a variable
    // before ever arithmetic operationing on it
    // so it must have already been written to
}

static void emit_sub_constant(CodeGen *codegen, int reg_index, int imm_val) {
    int order = codegen->regs->regs[reg_index].order;
    int amount = mod_reduce(imm_val, order);
    if (amount == 0) {
        return;
    }

    Alg alg = {0};
    Alg inverse_alg = {0};
    Alg realised = {0};
    parse_register_algorithm(codegen, reg_index, &alg);
    alg_invert(&alg, &inverse_alg);
    alg_power_realise(&inverse_alg, amount, order, &realised);
    emit_alg_if_nonempty(codegen, &realised);
    alg_free(&realised);
    alg_free(&inverse_alg);
    alg_free(&alg);
}

// after the desugarer, all let statements are guaranteed to just be of the form
// let x : int := 0
// and the actual value is an assign statement later. So just zero the register.
static void emit_let_statement(CodeGen *codegen, char *var_name) {
    int var_reg_ind = register_for_var(codegen, var_name);
    emit_zero(codegen, var_reg_ind);
    codegen->written[var_reg_ind] = true;
}

// assign constant statements are equivalent to zeroing the required register
// and adding the immediate value
// reg_index taken to allow temp to also be assigned a constant easier
static void emit_assign_constant(CodeGen *codegen, int reg_index, int imm_val) {
    emit_zero(codegen, reg_index);
    emit_add_constant(codegen, reg_index, imm_val);
    codegen->written[reg_index] = true;
}

// for expression of the form x = y
static void emit_assign_variable(CodeGen *codegen, int res_reg_index, int val_reg_index) {
    emit_zero(codegen, res_reg_index);
    emit_add_variable(codegen, res_reg_index, val_reg_index);
}

// requires use of temp register, which is always the second register in the table
// i.e. index 1. structure is as follows:
/*
zero temp
while y!=0
    y--
    temp++
while temp != 0
    temp--
    x++
    y++
*/
static void emit_add_variable(CodeGen *codegen, int res_reg_index, int val_reg_index) {
    require_temp_can_hold(codegen, val_reg_index, "add variable");
    emit_zero(codegen, TEMP_REGISTER_INDEX);

    // first while loop: (store y into temp)
    char load_temp_label[LABEL_BUFFER_SIZE];
    char done_label[LABEL_BUFFER_SIZE];
    char val_cycles[CYCLE_LIST_BUFFER_SIZE];
    fresh_label(codegen, load_temp_label, sizeof load_temp_label, "load_temp");
    fresh_label(codegen, done_label, sizeof done_label, "load_temp_done");
    cycle_list(codegen, val_reg_index, val_cycles, sizeof val_cycles);

    emit_label(codegen, load_temp_label);
    emit_branch(codegen, val_cycles, done_label);
    emit_add_constant(codegen, TEMP_REGISTER_INDEX, 1); // temp++
    emit_sub_constant(codegen, val_reg_index, 1); // y--
    emit_goto(codegen, load_temp_label);    
    emit_label(codegen, done_label);

    // second while loop: (restore y and x)
    char add_x_label[LABEL_BUFFER_SIZE];
    char add_done_label[LABEL_BUFFER_SIZE];
    char temp_cycles[CYCLE_LIST_BUFFER_SIZE];
    fresh_label(codegen, add_x_label, sizeof add_x_label, "add_variable");
    fresh_label(codegen, add_done_label, sizeof add_done_label, "add_variable_done");
    cycle_list(codegen, TEMP_REGISTER_INDEX, temp_cycles, sizeof temp_cycles);

    emit_label(codegen, add_x_label);
    emit_branch(codegen, temp_cycles, add_done_label);
    emit_sub_constant(codegen, TEMP_REGISTER_INDEX, 1); // temp--
    emit_add_constant(codegen, res_reg_index, 1); // x++
    emit_add_constant(codegen, val_reg_index, 1); // y++
    emit_goto(codegen, add_x_label);    
    emit_label(codegen, add_done_label);

}

// for expressions of the form x = x - y;
static void emit_sub_variable(CodeGen *codegen, int res_reg_index, int val_reg_index) {
    require_temp_can_hold(codegen, val_reg_index, "sub variable");
    emit_zero(codegen, TEMP_REGISTER_INDEX);

    // first while loop: (store y into temp)
    char load_temp_label[LABEL_BUFFER_SIZE];
    char done_label[LABEL_BUFFER_SIZE];
    char val_cycles[CYCLE_LIST_BUFFER_SIZE];
    fresh_label(codegen, load_temp_label, sizeof load_temp_label, "load_temp");
    fresh_label(codegen, done_label, sizeof done_label, "load_temp_done");
    cycle_list(codegen, val_reg_index, val_cycles, sizeof val_cycles);

    emit_label(codegen, load_temp_label);
    emit_branch(codegen, val_cycles, done_label);
    emit_add_constant(codegen, TEMP_REGISTER_INDEX, 1); // temp++
    emit_sub_constant(codegen, val_reg_index, 1); // y--
    emit_goto(codegen, load_temp_label);    
    emit_label(codegen, done_label);

    // second while loop: (restore y and x)
    char add_x_label[LABEL_BUFFER_SIZE];
    char add_done_label[LABEL_BUFFER_SIZE];
    char temp_cycles[CYCLE_LIST_BUFFER_SIZE];
    fresh_label(codegen, add_x_label, sizeof add_x_label, "add_variable");
    fresh_label(codegen, add_done_label, sizeof add_done_label, "add_variable_done");
    cycle_list(codegen, TEMP_REGISTER_INDEX, temp_cycles, sizeof temp_cycles);

    emit_label(codegen, add_x_label);
    emit_branch(codegen, temp_cycles, add_done_label);
    emit_sub_constant(codegen, TEMP_REGISTER_INDEX, 1); // temp--
    emit_sub_constant(codegen, res_reg_index, 1); // x--
    emit_add_constant(codegen, val_reg_index, 1); // y++
    emit_goto(codegen, add_x_label);    
    emit_label(codegen, add_done_label);

}

// Move the whole counter value from src into dst.
//
// Precondition:
//   dst is already solved/zero.
//
// Effect:
//   while src != 0:
//       src--
//       dst++
//
// Final state:
//   dst = old dst + old src, but because old dst is zero, dst = old src.
//   src = 0.
//
// This is destructive: it transfers the value rather than copying it.
static void emit_move_register_destructive(CodeGen *codegen, int dst_reg_index, int src_reg_index) {
    if (dst_reg_index == TEMP_REGISTER_INDEX) {
        require_temp_can_hold(codegen, src_reg_index, "destructive move into temp");
    }

    char move_label[LABEL_BUFFER_SIZE];
    char done_label[LABEL_BUFFER_SIZE];
    char src_cycles[CYCLE_LIST_BUFFER_SIZE];
    fresh_label(codegen, move_label, sizeof move_label, "move");
    fresh_label(codegen, done_label, sizeof done_label, "move_done");
    cycle_list(codegen, src_reg_index, src_cycles, sizeof src_cycles);

    emit_label(codegen, move_label);
    emit_branch(codegen, src_cycles, done_label);
    emit_sub_constant(codegen, src_reg_index, 1);
    emit_add_constant(codegen, dst_reg_index, 1);
    emit_goto(codegen, move_label);
    emit_label(codegen, done_label);

    codegen->written[dst_reg_index] = true;
    codegen->written[src_reg_index] = true;

}

static void emit_comparison_of_variable_and_immediate(CodeGen *codegen,
                                                      int var_reg_index,
                                                      int imm_val,
                                                      const char *true_label,
                                                      const char *false_label) {
    int cmp_val = mod_reduce(imm_val, codegen->regs->regs[var_reg_index].order);
    char restore_true_label[LABEL_BUFFER_SIZE];
    char var_cycles[CYCLE_LIST_BUFFER_SIZE];
    fresh_label(codegen, restore_true_label, sizeof restore_true_label,
                "cmp_restore_true");
    cycle_list(codegen, var_reg_index, var_cycles, sizeof var_cycles);

    /*
     * Test old x == N by temporarily shifting x to x - N and checking whether
     * the register is solved. Both outgoing paths restore x before jumping to
     * user code, so this works even when x is the high-order _io/R0 register.
     */
    emit_sub_constant(codegen, var_reg_index, cmp_val);
    emit_branch(codegen, var_cycles, restore_true_label);

    emit_add_constant(codegen, var_reg_index, cmp_val);
    emit_goto(codegen, false_label);

    emit_label(codegen, restore_true_label);
    emit_add_constant(codegen, var_reg_index, cmp_val);
    emit_goto(codegen, true_label);

    codegen->written[var_reg_index] = true;

}

// Complete the destructive setup for testing old x == old y.
//
// Precondition:
//   R1 = old x
//   x = 0
//   y = old y
//
// Effect:
//   while y != 0:
//       y--
//       x++
//       R1--
//
// Final state:
//   x = old y
//   y = 0
//   R1 = old x - old y
//
// After this, R1 being solved means the original x and y were equal.
static void emit_variable_comparison_subtract_y(CodeGen *codegen,
                                                int x_reg_index,
                                                int y_reg_index) {
    char loop_label[LABEL_BUFFER_SIZE];
    char done_label[LABEL_BUFFER_SIZE];
    char y_cycles[CYCLE_LIST_BUFFER_SIZE];
    fresh_label(codegen, loop_label, sizeof loop_label, "cmp_var_sub_y");
    fresh_label(codegen, done_label, sizeof done_label, "cmp_var_sub_y_done");
    cycle_list(codegen, y_reg_index, y_cycles, sizeof y_cycles);

    emit_label(codegen, loop_label);
    emit_branch(codegen, y_cycles, done_label);
    emit_sub_constant(codegen, y_reg_index, 1);
    emit_add_constant(codegen, x_reg_index, 1);
    emit_sub_constant(codegen, TEMP_REGISTER_INDEX, 1);
    emit_goto(codegen, loop_label);
    emit_label(codegen, done_label);

    codegen->written[x_reg_index] = true;
    codegen->written[y_reg_index] = true;
    codegen->written[TEMP_REGISTER_INDEX] = true;

}

// Restore the state after emit_variable_comparison_subtract_y, then jump.
//
// Precondition:
//   x = old y
//   y = 0
//   R1 = old x - old y
//
// Restore phase 1:
//   while x != 0:
//       x--
//       y++
//       R1++
//
// Now:
//   x = 0
//   y = old y
//   R1 = old x
//
// Restore phase 2:
//   while R1 != 0:
//       R1--
//       x++
//
// Final state:
//   x = old x
//   y = old y
//   R1 = 0
static void emit_restore_variable_comparison_and_goto(CodeGen *codegen,
                                                      int x_reg_index,
                                                      int y_reg_index,
                                                      const char *target_label,
                                                      const char *prefix) {
    char restore_y_label[LABEL_BUFFER_SIZE];
    char restore_x_label[LABEL_BUFFER_SIZE];
    char x_cycles[CYCLE_LIST_BUFFER_SIZE];
    char temp_cycles[CYCLE_LIST_BUFFER_SIZE];
    fresh_label(codegen, restore_y_label, sizeof restore_y_label, prefix);
    fresh_label(codegen, restore_x_label, sizeof restore_x_label, prefix);
    cycle_list(codegen, x_reg_index, x_cycles, sizeof x_cycles);
    cycle_list(codegen, TEMP_REGISTER_INDEX, temp_cycles, sizeof temp_cycles);

    emit_label(codegen, restore_y_label);
    emit_branch(codegen, x_cycles, restore_x_label);
    emit_sub_constant(codegen, x_reg_index, 1);
    emit_add_constant(codegen, y_reg_index, 1);
    emit_add_constant(codegen, TEMP_REGISTER_INDEX, 1);
    emit_goto(codegen, restore_y_label);

    emit_label(codegen, restore_x_label);
    emit_branch(codegen, temp_cycles, target_label);
    emit_sub_constant(codegen, TEMP_REGISTER_INDEX, 1);
    emit_add_constant(codegen, x_reg_index, 1);
    emit_goto(codegen, restore_x_label);

}

static void emit_comparison_of_variables(CodeGen *codegen,
                                         int x_reg_index,
                                         int y_reg_index,
                                         const char *true_label,
                                         const char *false_label) {
    if (x_reg_index == y_reg_index) {
        emit_goto(codegen, true_label);
        return;
    }

    require_temp_can_hold(codegen, x_reg_index, "variable comparison");
    require_temp_can_hold(codegen, y_reg_index, "variable comparison");

    char restore_true_label[LABEL_BUFFER_SIZE];
    char temp_cycles[CYCLE_LIST_BUFFER_SIZE];
    fresh_label(codegen, restore_true_label, sizeof restore_true_label,
                "cmp_var_restore_true");
    cycle_list(codegen, TEMP_REGISTER_INDEX, temp_cycles, sizeof temp_cycles);

    // R1 = old x, x = 0.
    emit_zero(codegen, TEMP_REGISTER_INDEX);
    emit_move_register_destructive(codegen, TEMP_REGISTER_INDEX, x_reg_index);

    // x = old y, y = 0, R1 = old x - old y.
    emit_variable_comparison_subtract_y(codegen, x_reg_index, y_reg_index);

    // If R1 is solved, old x == old y. Both paths restore before jumping out.
    emit_branch(codegen, temp_cycles, restore_true_label);

    emit_restore_variable_comparison_and_goto(codegen, x_reg_index, y_reg_index,
                                              false_label,
                                              "cmp_var_restore_false_loop");

    emit_label(codegen, restore_true_label);
    emit_restore_variable_comparison_and_goto(codegen, x_reg_index, y_reg_index,
                                              true_label,
                                              "cmp_var_restore_true_loop");

    codegen->written[x_reg_index] = true;
    codegen->written[y_reg_index] = true;
    codegen->written[TEMP_REGISTER_INDEX] = true;

}

// Convert a COND_SOLVED piece list from the AST into branch syntax:
//   "UF,UFR,UR"
// For solved[] with no pieces, this returns the empty string, producing cycle().
static char *solved_piece_list(Cond *cond) {
    size_t needed = 1;
    for (int i = 0; i < cond->solved.count; i++) {
        needed += strlen(cond->solved.pieces[i]);
        if (i > 0) {
            needed++;
        }
    }

    char *out = malloc(needed);
    if (out == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, -1, "malloc failed");
    }

    out[0] = '\0';
    for (int i = 0; i < cond->solved.count; i++) {
        if (i > 0) {
            strcat(out, ",");
        }
        strcat(out, cond->solved.pieces[i]);
    }

    return out;
}

static void append_alg_string(CodeGen *codegen, Alg *out, const char *text, int line) {
    Alg parsed = {0};
    if (!alg_parse(text, &parsed)) {
        alg_free(out);
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, line,
            "invalid algorithm reached codegen");
    }

    alg_concat(out, &parsed);
    alg_free(&parsed);
    (void)codegen;
}

static void append_alg_expr(CodeGen *codegen, Alg *out, Expr *expr) {
    if (expr == NULL) {
        alg_free(out);
        die_internal_null;
    }

    switch (expr->kind) {
        case EXPR_ALG_LIT: {
            char *s = alg_to_string(expr->alg_val);
            append_alg_string(codegen, out, s, 0);
            free(s);
            break;
        }
        case EXPR_VAR: {
            VarEntry *entry = typeenv_lookup(codegen->env, expr->var_name);
            if (entry == NULL || entry->type != TYPE_ALG ||
                !entry->alg_known || entry->alg_value == NULL) {
                alg_free(out);
                die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                    "alg variable '%s' missing or unknown during codegen",
                    expr->var_name);
            }
            append_alg_string(codegen, out, entry->alg_value, 0);
            break;
        }
        case EXPR_CONCAT:
            append_alg_expr(codegen, out, expr->bin_op.LHS);
            append_alg_expr(codegen, out, expr->bin_op.RHS);
            break;
        case EXPR_INT_LIT:
        case EXPR_ADD:
        case EXPR_SUB:
        case EXPR_NEG:
        case EXPR_ORD:
        case EXPR_SOLVED:
        case EXPR_NOT:
        case EXPR_EQ:
        case EXPR_LT:
        case EXPR_GT:
        case EXPR_LEQ:
        case EXPR_GEQ:
            alg_free(out);
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                "non-alg expression reached apply codegen");
    }
}

static void emit_apply(CodeGen *codegen, Expr *alg_expr) {
    Alg alg = {0};
    append_alg_expr(codegen, &alg, alg_expr);
    alg_simplify(&alg);
    emit_alg_if_nonempty(codegen, &alg);
    alg_free(&alg);
}

// Collapse pairs of nots before emitting condition code:
//   not not c         -> c
//   not not not c     -> not c
//   not not not not c -> c
//
// A remaining single not is handled by swapping the true/false labels in
// emit_condition, which works for both if and while.
static Cond *denest_not_pairs(Cond *cond) {
    while (cond != NULL && cond->kind == COND_NOT &&
           cond->operand != NULL && cond->operand->kind == COND_NOT) {
        cond = cond->operand->operand;
    }

    return cond;
}

// emits the code required to evaluate the condition.
// depending on the type of condition, logic may be different
// at the end, it will jump to either true_label or false_label.
static void emit_condition(CodeGen *codegen, Cond *cond, const char *true_label,
                           const char *false_label) {
    cond = denest_not_pairs(cond);
    if (cond == NULL) {
        die_internal_null;
    }

    switch (cond->kind) {
        case COND_EQ_INT: // e.g. x = N
            emit_comparison_of_variable_and_immediate(
                codegen,
                register_for_var(codegen, cond->eq_int.var),
                cond->eq_int.val,
                true_label,
                false_label
            );
            break;
        case COND_EQ_VAR:
            emit_comparison_of_variables(
                codegen,
                register_for_var(codegen, cond->eq_var.lhs),
                register_for_var(codegen, cond->eq_var.rhs),
                true_label,
                false_label
            );
            break;
        case COND_NOT:
            emit_condition(codegen, cond->operand, false_label, true_label);
            break;
        case COND_SOLVED: {
            char *pieces = solved_piece_list(cond);
            emit_branch(codegen, pieces, true_label);
            emit_goto(codegen, false_label);
            free(pieces);
            break;
        }
    }
}

// we provide labels since for if statements we will recursively codegen them
static void compile_if(CodeGen *codegen, Statement *if_stmt)
{
    char cond_true_label[LABEL_BUFFER_SIZE];
    char cond_false_label[LABEL_BUFFER_SIZE];
    char cond_end_label[LABEL_BUFFER_SIZE];
    fresh_label(codegen, cond_true_label, sizeof cond_true_label, "cond_true");
    fresh_label(codegen, cond_false_label, sizeof cond_false_label, "cond_false");
    fresh_label(codegen, cond_end_label, sizeof cond_end_label, "cond_end");

    Cond *cond = convert_expr_into_cond(if_stmt->if_stmt.if_cond);
    emit_condition(codegen, cond, cond_true_label, cond_false_label);
    free_cond(cond);

    // assemble the code for cond_true_label (i.e. if body)
    emit_label(codegen, cond_true_label);
    compile_statement(codegen, if_stmt->if_stmt.if_then);
    // goto end
    emit_goto(codegen, cond_end_label);
    // assemble the code for false branch (i.e. else branch)
    emit_label(codegen, cond_false_label);
    if (if_stmt->if_stmt.if_else != NULL) {
        compile_statement(codegen, if_stmt->if_stmt.if_else);
    }
    // falls through to end anyways
    // end if label
    emit_label(codegen, cond_end_label);

}


static void compile_while(CodeGen *codegen, Statement *while_stmt)
{
    char cond_label[LABEL_BUFFER_SIZE];
    char body_label[LABEL_BUFFER_SIZE];
    char end_label[LABEL_BUFFER_SIZE];
    fresh_label(codegen, cond_label, sizeof cond_label, "while_cond");
    fresh_label(codegen, body_label, sizeof body_label, "while_body");
    fresh_label(codegen, end_label, sizeof end_label, "while_end");

    Cond *cond = convert_expr_into_cond(while_stmt->while_stmt.while_cond);

    emit_label(codegen, cond_label);
    emit_condition(codegen, cond, body_label, end_label);

    emit_label(codegen, body_label);
    compile_statement(codegen, while_stmt->while_stmt.while_body);
    emit_goto(codegen, cond_label);

    emit_label(codegen, end_label);
    free_cond(cond);

}

// ----------------------- INTERFACE --------------

static bool expr_is_var_named(Expr *expr, const char *name) {
    return expr != NULL && expr->kind == EXPR_VAR && strcmp(expr->var_name, name) == 0;
}

static void compile_assignment(CodeGen *codegen, Statement *statement) {
    VarEntry *target_entry = typeenv_lookup(codegen->env, statement->assign.assign_name);
    if (target_entry == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
            "assignment target '%s' missing during codegen",
            statement->assign.assign_name);
    }

    if (target_entry->type == TYPE_ALG) {
        return;
    }

    int target_reg = register_for_var(codegen, statement->assign.assign_name);
    Expr *rhs = statement->assign.assign_value;

    if (rhs == NULL) {
        die_internal_null;
    }

    switch (rhs->kind) {
        case EXPR_INT_LIT:
            emit_assign_constant(codegen, target_reg, rhs->int_val);
            break;
        case EXPR_VAR:
            emit_assign_variable(codegen, target_reg, register_for_var(codegen, rhs->var_name));
            break;
        case EXPR_ADD:
            if (!expr_is_var_named(rhs->bin_op.LHS, statement->assign.assign_name)) {
                die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                    "non-primitive assignment reached codegen");
            }
            if (rhs->bin_op.RHS->kind == EXPR_INT_LIT) {
                emit_add_constant(codegen, target_reg, rhs->bin_op.RHS->int_val);
            } else if (rhs->bin_op.RHS->kind == EXPR_VAR) {
                emit_add_variable(codegen, target_reg,
                                  register_for_var(codegen, rhs->bin_op.RHS->var_name));
            } else {
                die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                    "non-primitive assignment reached codegen");
            }
            break;
        case EXPR_SUB:
            if (!expr_is_var_named(rhs->bin_op.LHS, statement->assign.assign_name)) {
                die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                    "non-primitive assignment reached codegen");
            }
            if (rhs->bin_op.RHS->kind == EXPR_INT_LIT) {
                emit_sub_constant(codegen, target_reg, rhs->bin_op.RHS->int_val);
            } else if (rhs->bin_op.RHS->kind == EXPR_VAR) {
                emit_sub_variable(codegen, target_reg,
                                  register_for_var(codegen, rhs->bin_op.RHS->var_name));
            } else {
                die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                    "non-primitive assignment reached codegen");
            }
            break;
        default:
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                "non-primitive assignment reached codegen");
    }
}

static void compile_let(CodeGen *codegen, Statement *statement) {
    if (statement->decl.decl_type == TYPE_ALG) {
        return;
    }

    int target_reg = register_for_var(codegen, statement->decl.decl_name);
    emit_let_statement(codegen, statement->decl.decl_name);

    Expr *init = statement->decl.decl_value;
    if (init == NULL) {
        return;
    }

    switch (init->kind) {
        case EXPR_INT_LIT:
            emit_add_constant(codegen, target_reg, init->int_val);
            break;
        case EXPR_VAR:
            emit_add_variable(codegen, target_reg, register_for_var(codegen, init->var_name));
            break;
        default:
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                "non-primitive let reached codegen");
    }
}

static void compile_statement(CodeGen *codegen, Statement *statement) {
    switch (statement->kind) {
        case STMT_DECL:
            compile_let(codegen, statement);
            break;
        case STMT_ASSIGN:
            compile_assignment(codegen, statement);
            break;
        case STMT_IF:
            compile_if(codegen, statement);
            break;
        case STMT_WHILE:
            compile_while(codegen, statement);
            break;
        case STMT_GOTO:
            emit_goto(codegen, statement->goto_label);
            break;
        case STMT_LABEL:
            emit_label(codegen, statement->label_name);
            break;
        case STMT_INPUT: {
            if (!codegen->regs->r0_reserved) {
                die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                    "input reached codegen while R0 is not reserved");
            }
            emit(codegen, "input \"%s\"", statement->input_prompt);
            break;
        }
        case STMT_OUTPUT: {
            Expr *out_expr = statement->output_expr;
            int src_reg = IO_REGISTER_INDEX;
            if (out_expr != NULL && out_expr->kind != EXPR_VAR) {
                die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                    "non-variable output expression reached codegen");
            }
            if (out_expr == NULL) {
                if (!codegen->regs->r0_reserved) {
                    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                        "bare output reached codegen while R0 is not reserved");
                }
            } else {
                src_reg = register_for_var(codegen, out_expr->var_name);
            }
            char out_cycles[CYCLE_LIST_BUFFER_SIZE];
            cycle_list(codegen, src_reg, out_cycles, sizeof out_cycles);
            char *inverse_alg = register_inverse_algorithm_string(codegen, src_reg);
            emit(codegen, "output (%s) (%s)", inverse_alg, out_cycles);
            free(inverse_alg);
            break;
        }
        case STMT_APPLY:
            emit_apply(codegen, statement->apply_expr);
            break;
        case STMT_BLOCK:
            compile_statements(codegen, statement->block.block_stmts,
                               statement->block.block_count);
            break;
    }
}

static void compile_statements(CodeGen *codegen, Statement **statements, int count) {
    for (int i = 0; i < count; i++) {
        compile_statement(codegen, statements[i]);
    }
}

void codegen_program(FILE *out, ProgramAST *program, TypeEnv *type_env, RegTable *regs, const int *colouring) {
    if (out == NULL || program == NULL || type_env == NULL || regs == NULL || colouring == NULL) {
        die_internal_null;
    }

    ensure_codegen_register_contract(regs);
    CodeGen *codegen = codegen_init(out, program, type_env, regs, colouring);
    emit_register_info(codegen);
    compile_statements(codegen, program->statements, program->count);
    codegen_free(codegen);
}
