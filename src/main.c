#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/codegen.h"
#include "../include/cube.h"
#include "../include/desugarer.h"
#include "../include/interference.h"
#include "../include/liveness.h"
#include "../include/print_ast.h"
#include "../include/program_ast.h"
#include "../include/regalloc.h"
#include "../include/typechecker.h"
#include "../include/util.h"

#if defined(__GNUC__) || defined(__clang__)
#define WEAK_STAGE __attribute__((weak))
#else
#define WEAK_STAGE
#endif

/*
 * Some stage implementations are still omitted from extension/Makefile while
 * the team migrates modules to the current AST. Declaring the stage entry
 * points weak lets this main file link during that parallel development.
 *
 * When a real object file is linked, the real implementation is called. When a
 * stage is absent, require_stage() reports the missing module before use; it
 * never silently skips or fakes a compiler pass.
 */
extern void pretty_print_stmt(const Statement *stmt, int indent) WEAK_STAGE;
extern void typeenv_init(TypeEnv *env) WEAK_STAGE;
extern void typeenv_free(TypeEnv *env) WEAK_STAGE;
extern void typecheck_program(ProgramAST *program, TypeEnv *env) WEAK_STAGE;
extern ProgramAST *desugared_statement_ast(ProgramAST *program_ast,
                                           TypeEnv *type_env) WEAK_STAGE;
extern CFG *cfg_build(ProgramAST *program) WEAK_STAGE;
extern void cfg_free(CFG *cfg) WEAK_STAGE;
extern void cfg_dump(const CFG *cfg, FILE *out) WEAK_STAGE;
extern LivenessResult *liveness_analyze(CFG *cfg, TypeEnv *env) WEAK_STAGE;
extern void liveness_free(LivenessResult *result) WEAK_STAGE;
extern void liveness_dump(const LivenessResult *result, FILE *out) WEAK_STAGE;
extern InterferenceGraph *ig_build(TypeEnv *env, const IGLivenessNode *nodes,
                                   int node_count) WEAK_STAGE;
extern void ig_free(InterferenceGraph *ig) WEAK_STAGE;
extern void ig_dump(InterferenceGraph *ig, FILE *out) WEAK_STAGE;
extern void regalloc_init(RegTable *table, int temp_required_order) WEAK_STAGE;
extern void regalloc_free(RegTable *table) WEAK_STAGE;
extern int *regalloc_run(RegTable *table, const InterferenceGraph *ig) WEAK_STAGE;
extern void regalloc_dump(const RegTable *table, const InterferenceGraph *ig,
                          const int *coloring, FILE *out) WEAK_STAGE;
extern void codegen_program(FILE *out, ProgramAST *program, TypeEnv *type_env,
                            RegTable *regs, const int *colouring) WEAK_STAGE;

typedef struct {
    const char *input_file_name;
    const char *output_file_name;

    bool dump_ast;
    bool dump_desugar;
    bool dump_cfg;
    bool dump_liveness;
    bool dump_ig;
    bool dump_regs;
    bool dump_preprocessor;

    /*
     * Parsed for command-line compatibility with the spec. The public codegen
     * API currently has no parameter for physical mode, so main records the
     * flag but cannot pass it on yet.
     */
    bool physical;
} MainOptions;

static void require_stage(const void *fn, const char *stage_name) {
    if (fn == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
            "pipeline stage '%s' is not linked", stage_name);
    }
}

static void usage(const char *program_name) {
    die(EXIT_FAILURE, "main", 0,
        "Usage: %s [--dump-ast] [--dump-desugar] [--dump-cfg] "
        "[--dump-liveness] [--dump-ig] [--dump-regs] [--dump-preprocessor] "
        "[--physical] <input.cbyte> <output.s>",
        program_name);
}

static bool parse_flag(MainOptions *opts, const char *arg) {
    if (strcmp(arg, "--dump-ast") == 0) {
        opts->dump_ast = true;
    } else if (strcmp(arg, "--dump-desugar") == 0) {
        opts->dump_desugar = true;
    } else if (strcmp(arg, "--dump-cfg") == 0) {
        opts->dump_cfg = true;
    } else if (strcmp(arg, "--dump-liveness") == 0) {
        opts->dump_liveness = true;
    } else if (strcmp(arg, "--dump-ig") == 0) {
        opts->dump_ig = true;
    } else if (strcmp(arg, "--dump-regs") == 0) {
        opts->dump_regs = true;
    } else if (strcmp(arg, "--dump-preprocessor") == 0) {
        opts->dump_preprocessor = true;
    } else if (strcmp(arg, "--physical") == 0) {
        opts->physical = true;
    } else {
        return false;
    }

    return true;
}

static MainOptions parse_options(int argc, char **argv) {
    MainOptions opts = {0};

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strncmp(arg, "--", 2) == 0) {
            if (!parse_flag(&opts, arg)) {
                die(EXIT_FAILURE, "main", 0, "unknown flag '%s'", arg);
            }
            continue;
        }

        if (opts.input_file_name == NULL) {
            opts.input_file_name = arg;
        } else if (opts.output_file_name == NULL) {
            opts.output_file_name = arg;
        } else {
            usage(argv[0]);
        }
    }

    if (opts.input_file_name == NULL || opts.output_file_name == NULL) {
        usage(argv[0]);
    }

    return opts;
}

static void dump_program(const ProgramAST *program) {
    if (program == NULL) {
        return;
    }

    require_stage((const void *)pretty_print_stmt, "print_ast");
    for (int i = 0; i < program->count; i++) {
        pretty_print_stmt(program->statements[i], 0);
    }
}

// Stream the preprocessor's <input-base>-pp.cbyte output to stdout. The
// preprocessor always writes that file as part of parse_program; we just copy
// it back out for inspection. Recomputes the suffix the same way parse_program
// does, since preprocess() owns the write but not the resulting path string.
static void dump_preprocessor_output(const char *input_file_name) {
    static const char suffix[] = ".cbyte";
    const size_t suffix_len = sizeof(suffix) - 1;
    const size_t input_length = strlen(input_file_name);

    if (input_length < suffix_len ||
        strcmp(input_file_name + input_length - suffix_len, suffix) != 0) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
            "dump-preprocessor: expected filename ending in '.cbyte', got '%s'",
            input_file_name);
    }

    const size_t base_length = input_length - suffix_len;
    char *preprocessed_path = malloc(base_length + sizeof("-pp.cbyte"));
    if (preprocessed_path == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
            "Malloc failed while building preprocessor output path");
    }
    memcpy(preprocessed_path, input_file_name, base_length);
    strcpy(preprocessed_path + base_length, "-pp.cbyte");

    FILE *input = fopen(preprocessed_path, "r");
    if (input == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
            "dump-preprocessor: failed to open '%s': %s",
            preprocessed_path, strerror(errno));
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), input)) > 0) {
        if (fwrite(buf, 1, n, stdout) != n) {
            fclose(input);
            free(preprocessed_path);
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                "dump-preprocessor: failed to write to stdout: %s",
                strerror(errno));
        }
    }

    fclose(input);
    free(preprocessed_path);
}

static bool expr_uses_io_register(const Expr *expr) {
    if (expr == NULL) {
        return false;
    }

    switch (expr->kind) {
        case EXPR_VAR:
        case EXPR_ORD:
            return strcmp(expr->var_name, R0_SYNTHETIC_VARIABLE) == 0;
        case EXPR_ADD:
        case EXPR_SUB:
        case EXPR_CONCAT:
        case EXPR_EQ:
        case EXPR_LT:
        case EXPR_GT:
        case EXPR_LEQ:
        case EXPR_GEQ:
            return expr_uses_io_register(expr->bin_op.LHS) ||
                   expr_uses_io_register(expr->bin_op.RHS);
        case EXPR_NEG:
        case EXPR_NOT:
            return expr_uses_io_register(expr->unary_op);
        case EXPR_INT_LIT:
        case EXPR_ALG_LIT:
        case EXPR_SOLVED:
            return false;
    }

    return false;
}

static bool statement_uses_io_register(const Statement *stmt) {
    if (stmt == NULL) {
        return false;
    }

    switch (stmt->kind) {
        case STMT_INPUT:
            return true;
        case STMT_OUTPUT:
            return stmt->output_expr == NULL ||
                   expr_uses_io_register(stmt->output_expr);
        case STMT_DECL:
            return expr_uses_io_register(stmt->decl.decl_value);
        case STMT_ASSIGN:
            return strcmp(stmt->assign.assign_name, R0_SYNTHETIC_VARIABLE) == 0 ||
                   expr_uses_io_register(stmt->assign.assign_value);
        case STMT_APPLY:
            return expr_uses_io_register(stmt->apply_expr);
        case STMT_IF:
            if (expr_uses_io_register(stmt->if_stmt.if_cond)) {
                return true;
            }
            return statement_uses_io_register(stmt->if_stmt.if_then) ||
                   statement_uses_io_register(stmt->if_stmt.if_else);
        case STMT_WHILE:
            if (expr_uses_io_register(stmt->while_stmt.while_cond)) {
                return true;
            }
            return statement_uses_io_register(stmt->while_stmt.while_body);
        case STMT_BLOCK:
            for (int i = 0; i < stmt->block.block_count; i++) {
                if (statement_uses_io_register(stmt->block.block_stmts[i])) {
                    return true;
                }
            }
            return false;
        case STMT_GOTO:
        case STMT_LABEL:
            return false;
    }

    return false;
}

static bool program_uses_io_register(const ProgramAST *program) {
    if (program == NULL) {
        return false;
    }

    for (int i = 0; i < program->count; i++) {
        if (statement_uses_io_register(program->statements[i])) {
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv) {
    const MainOptions opts = parse_options(argc, argv);
    (void)opts.physical;

    ProgramAST *parsed_program = NULL;
    ProgramAST *desugared_program = NULL;
    TypeEnv type_env = {0};
    bool type_env_initialised = false;
    CFG *cfg = NULL;
    LivenessResult *liveness = NULL;
    InterferenceGraph *ig = NULL;
    RegTable regs = {0};
    bool regs_initialised = false;
    int *colouring = NULL;
    FILE *out = NULL;

    /*
     * Stage 1: parse source text into the high-level AST.
     *
     * parse_program owns file reading and lexer/parser setup internally. The
     * returned ProgramAST is owned by main and is freed on every successful
     * exit path below.
     */
    parsed_program = init_program_ast();
    parse_program(opts.input_file_name, parsed_program);
    if (opts.dump_preprocessor) {
        dump_preprocessor_output(opts.input_file_name);
    }
    if (opts.dump_ast) {
        dump_program(parsed_program);
    }

    /*
     * Stage 2: semantic analysis.
     *
     * typeenv_init predeclares the synthetic R0 variable and allocates the
     * environment's backing array. typecheck_program then validates names,
     * labels, expression types, and compile-time alg values, mutating the AST
     * and TypeEnv for later passes.
     */
    require_stage((const void *)typeenv_init, "typechecker");
    require_stage((const void *)typecheck_program, "typechecker");
    typeenv_init(&type_env);
    type_env_initialised = true;
    typecheck_program(parsed_program, &type_env);

    /*
     * Stage 3: initialise shared cube tables before any pass asks cube.c for
     * algorithm order, cycle sets, or generated register algorithms.
     *
     * The desugarer can fold ord(algVar) via compute_order(), and regalloc_init
     * explicitly requires cube_init() before it builds R0's register metadata.
     * Keeping the call here centralises global module initialisation.
     */
    cube_init();

    /*
     * Stage 4: desugar high-level integer expressions into primitive forms.
     *
     * desugared_statement_ast may add synthetic temporaries to type_env. The
     * returned ProgramAST is the one consumed by CFG/liveness/regalloc/codegen.
     * The current desugarer transfers statement ownership out of parsed_program
     * and leaves it empty, so freeing both AST handles at the end is safe.
     */
    require_stage((const void *)desugared_statement_ast, "desugarer");
    desugared_program = desugared_statement_ast(parsed_program, &type_env);
    if (opts.dump_desugar) {
        dump_program(desugared_program);
    }

    /*
     * Stage 5: build the control-flow graph over the desugared program.
     *
     * cfg_build owns the returned CFG. Its nodes borrow Statement pointers from
     * desugared_program, so the CFG is freed before the AST.
     */
    require_stage((const void *)cfg_build, "liveness/cfg");
    cfg = cfg_build(desugared_program);
    if (opts.dump_cfg) {
        require_stage((const void *)cfg_dump, "liveness/cfg dump");
        cfg_dump(cfg, stdout);
    }

    /*
     * Stage 6: run backwards liveness over the CFG.
     *
     * liveness_analyze borrows cfg and type_env. Its owned ig_nodes array is
     * passed directly to ig_build in the next stage.
     */
    require_stage((const void *)liveness_analyze, "liveness");
    liveness = liveness_analyze(cfg, &type_env);
    if (opts.dump_liveness) {
        require_stage((const void *)liveness_dump, "liveness dump");
        liveness_dump(liveness, stdout);
    }

    /*
     * Stage 7: construct the interference graph from liveness results.
     *
     * The graph tells register allocation which int variables cannot share a
     * physical cube register. It borrows names and indices from type_env.
     */
    require_stage((const void *)ig_build, "interference");
    ig = ig_build(&type_env, liveness->ig_nodes, liveness->node_count);
    if (opts.dump_ig) {
        require_stage((const void *)ig_dump, "interference dump");
        ig_dump(ig, stdout);
    }

    /*
     * Stage 8: allocate cube registers.
     *
     * regalloc_init creates R0 and the reserved temporary register entries.
     * regalloc_run colours every int variable in the interference graph and
     * returns an owned int array indexed by TypeEnv variable index.
     */
    require_stage((const void *)regalloc_init, "regalloc");
    require_stage((const void *)regalloc_run, "regalloc");
    /* The scratch register R1 backs every variable add/sub and must be at least
     * as wide as the widest int variable, so reserve it against the largest
     * declared register order. */
    int temp_required_order = 0;
    for (int i = 0; i < type_env.count; i++) {
        if (type_env.entries[i].type == TYPE_INT &&
            type_env.entries[i].required_order > temp_required_order) {
            temp_required_order = type_env.entries[i].required_order;
        }
    }
    regalloc_init(&regs, temp_required_order);
    regs.r0_reserved = program_uses_io_register(desugared_program);
    regs_initialised = true;
    colouring = regalloc_run(&regs, ig);
    if (opts.dump_regs) {
        require_stage((const void *)regalloc_dump, "regalloc dump");
        regalloc_dump(&regs, ig, colouring, stdout);
    }

    /*
     * Stage 9: emit final assembly.
     *
     * codegen_program consumes the desugared AST, TypeEnv, register table, and
     * colouring. The FILE is owned by main and closed immediately after codegen
     * so write errors are reported before normal cleanup.
     */
    require_stage((const void *)codegen_program, "codegen");
    out = fopen(opts.output_file_name, "w");
    if (out == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
            "failed to open '%s' for writing: %s",
            opts.output_file_name, strerror(errno));
    }
    codegen_program(out, desugared_program, &type_env, &regs, colouring);
    if (fclose(out) != 0) {
        out = NULL;
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
            "failed to close '%s': %s",
            opts.output_file_name, strerror(errno));
    }
    out = NULL;

    /*
     * Normal cleanup in reverse dependency order:
     *   - codegen output is already closed;
     *   - colouring belongs to main;
     *   - regs owns algorithm strings;
     *   - ig and liveness borrow type_env/cfg respectively;
     *   - cfg borrows statements from desugared_program;
     *   - type_env outlives every pass that borrowed it.
     */
    free(colouring);
    if (regs_initialised) {
        require_stage((const void *)regalloc_free, "regalloc cleanup");
        regalloc_free(&regs);
    }
    if (ig != NULL) {
        require_stage((const void *)ig_free, "interference cleanup");
        ig_free(ig);
    }
    if (liveness != NULL) {
        require_stage((const void *)liveness_free, "liveness cleanup");
        liveness_free(liveness);
    }
    if (cfg != NULL) {
        require_stage((const void *)cfg_free, "cfg cleanup");
        cfg_free(cfg);
    }
    free_program_ast(desugared_program);
    free_program_ast(parsed_program);
    if (type_env_initialised) {
        require_stage((const void *)typeenv_free, "typechecker cleanup");
        typeenv_free(&type_env);
    }

    return EXIT_CODE_OK;
}
