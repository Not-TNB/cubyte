#ifndef TYPECHECKER_H
#define TYPECHECKER_H

#include <stdbool.h>

#include "program_ast.h"

#define TYPECHECKER_INITIAL_CAPACITY 8

/*
 * Public interface for the CuBit typechecker.
 *
 * Pipeline position:
 *   parser -> typechecker -> desugarer -> liveness/regalloc/codegen
 *
 * The parser checks syntax and builds the AST. The typechecker checks semantic
 * rules that need global context: declared names, textual declaration order,
 * expression types, labels/gotos, valid piece labels, and compile-time-known
 * alg values. On the first error, these functions call die(...), print an
 * error such as:
 *
 *   [typecheck] line N: message
 *
 * and exit with EXIT_CODE_TYPECHECK. Internal misuse exits through the
 * internal stage instead.
 */

typedef struct {
    /*
     * Variable name owned by this entry, except for the synthetic _io entry,
     * whose name points at the R0_SYNTHETIC_VARIABLE string literal.
     */
    char *name;

    /*
     * Source-level type of the variable. TYPE_UNKNOWN should not be stored in
     * the environment after a successful declaration.
     */
    TypeKind type;

    /*
     * Stable small id for later passes. It is currently the entry's slot in
     * TypeEnv.entries, so it remains valid as the dynamic array grows.
     */
    int index;

    /*
     * Compile-time alg value metadata.
     *
     * Source-level alg variables are compiler entities whose values are strings:
     *
     * let a : alg := "R U" ++ "R'"
     *
     * If type == TYPE_ALG, alg_known is true after successful typechecking and
     * alg_value points at an owned string. If type == TYPE_INT, alg_known is
     * false and alg_value is NULL. This is separate from physical register
     * algorithms assigned later by regalloc/codegen.
     */
    bool alg_known;
    char *alg_value;

    /* Minimum register order required for this int variable, from "let int:N x".
     * 0 means no constraint (any register works). Ignored for alg variables. */
    int required_order;
} VarEntry;

typedef struct {
    /*
     * Growable array of variable entries. The typechecker uses linear lookup
     * because programs are small and the spec allows it. Callers should treat
     * entries/count/capacity as owned implementation details, but later passes
     * may read entries after typecheck_program succeeds.
     */
    VarEntry *entries;
    int count;
    int capacity;
    /* Borrowed pointer to the source filename, set by typecheck_program.
     * Used to include file:line in typecheck error messages. May be NULL. */
    const char *source_filename;
} TypeEnv;

typedef struct {
    /*
     * Growable array used by the label pre-pass. Labels are collected before
     * the main statement walk so a goto may target a label that appears later
     * in the program text.
     */
    char **labels;
    int count;
    int capacity;
} LabelSet;

/*
 * Initialise an empty type environment and predeclare the synthetic variable
 * _io as an int at index 0.
 *
 * Ownership:
 *   - env must point at caller-owned storage.
 *   - typeenv_init allocates env->entries.
 *   - call typeenv_free exactly once when all later passes are done reading it.
 */
void typeenv_init(TypeEnv *env);

/*
 * Release all memory owned by env. Safe to call with NULL. After this returns,
 * env is reset to an empty zero-capacity state.
 */
void typeenv_free(TypeEnv *env);

/*
 * Return the environment entry for name, or NULL if name is not declared.
 * The returned pointer is borrowed and may be invalidated by a later
 * typeenv_declare that grows the underlying array.
 */
VarEntry *typeenv_lookup(TypeEnv *env, const char *name);

/*
 * Return the stable variable id for name, or -1 if name is undeclared.
 * This is the API liveness/regalloc should prefer when they need compact
 * integer positions instead of string comparisons.
 */
int typeenv_index(TypeEnv *env, const char *name);

/*
 * Add a new variable declaration.
 *
 * Rejects duplicate names. Also enforces the alg-name rule: alg variables may
 * not be named U, D, L, R, F, or B, because those names are reserved cube moves
 * in algorithm syntax. On success, assigns the next stable index and returns
 * true. On failure, exits through die().
 */
bool typeenv_declare(TypeEnv *env, const char *name, TypeKind type, int line);

/*
 * Initialise/free/query the temporary label set used by the pre-pass.
 *
 * labelset_init allocates internal storage. labelset_free releases copied
 * label strings and resets the structure. labelset_contains returns false for
 * NULL inputs and for missing labels.
 */
void labelset_init(LabelSet *labels);
void labelset_free(LabelSet *labels);
bool labelset_contains(LabelSet *labels, const char *name);

/*
 * Add one label definition to labels.
 *
 * The name is copied into the set. Duplicate labels are type errors because a
 * goto would otherwise have an ambiguous target. Returns true on success and
 * exits through die() on error.
 */
bool labelset_add(LabelSet *labels, const char *name, int line);

/*
 * Recursively collect every STMT_LABEL in program, including labels inside
 * if/else and while bodies. This pass does not validate goto statements; it
 * only builds the complete target set used by typecheck_statement later.
 *
 * labels must already have been initialised by labelset_init.
 */
void typecheck_collect_labels(ProgramAST *program, LabelSet *labels);

/*
 * Typecheck one expression and write expr->resolved_type.
 *
 * Rules enforced:
 *   - int literals resolve to TYPE_INT.
 *   - alg literals resolve to TYPE_ALG.
 *   - variables must already be declared and resolve to their env type.
 *   - +, -, and unary - require int operands and produce int.
 *   - ++ requires alg operands and produces alg.
 *   - ord(name) requires name to be a declared compile-time-known alg and
 *     produces int.
 *
 * Returns the resolved type. This mutates expr and all nested subexpressions.
 */
TypeKind typecheck_expr(TypeEnv *env, Expr *expr);

/*
 * Typecheck one condition.
 *
 * Rules enforced:
 *   - x = n: x must be a declared int variable.
 *   - x = y: both names must be declared int variables.
 *   - solved[p1, ...]: every listed piece must satisfy piece_from_string != -1.
 *   - not c: recursively typecheck c.
 *
 * In the current AST, conditions are ordinary Expr nodes (EXPR_EQ, EXPR_SOLVED,
 * EXPR_NOT, etc.), not a separate Cond type.
 */
void typecheck_cond(TypeEnv *env, Expr *cond);

/*
 * Typecheck one statement and any nested statements it owns.
 *
 * This function enforces textual scoping: a variable is usable only after its
 * declaration has been processed in source order, regardless of possible goto
 * control flow. For STMT_LET, the initializer is checked before the new name is
 * inserted, so "let x : int := x" is rejected.
 *
 * labels must contain the result of typecheck_collect_labels so STMT_GOTO can
 * be validated even for forward targets.
 */
void typecheck_statement(TypeEnv *env, LabelSet *labels, Statement *stmt);

/*
 * Typecheck a whole program.
 *
 * This is the normal entry point for the pass. The caller should pass an
 * already-initialised TypeEnv, usually created with typeenv_init. On success,
 * env contains all declared source variables, their stable indices, and known
 * alg values for TYPE_ALG entries. The AST has resolved_type populated on all
 * expressions reached by the walk.
 *
 * This pass does not decide which cube-register algorithm an int variable uses;
 * physical register algorithms are chosen/generated later.
 */
void typecheck_program(ProgramAST *program, TypeEnv *env);


// -------------- Helper for desugaring: --------------
// adds a temporary variable into type_env. Needed by desugarer for 
// if and while conditions
void typeenv_declare_temp(TypeEnv *env, const char *name, TypeKind type);

#endif
