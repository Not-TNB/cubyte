#include "../include/typechecker.h"

#include "../include/alg.h"
#include "../include/piece.h"
#include "../include/util.h"

#include <stdlib.h>
#include <string.h>

/*
    strdup is POSIX rather than standard C, so keep a tiny local version.
    declarations store their own copy of the name because parser-owned strings
    may be freed separately from the type environment.
 */
static char *typechecker_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "malloc failed");
    }

    memcpy(copy, s, len);
    return copy;
}

/*
    Single cube-move names are reserved in alg syntax. If we allowed:

        let R : alg := "U"

    then later alg expressions would become harder to read and validate.
 */
static bool is_forbidden_alg_name(const char *name) {
    return strcmp(name, "U") == 0 || strcmp(name, "D") == 0 ||
           strcmp(name, "L") == 0 || strcmp(name, "R") == 0 ||
           strcmp(name, "F") == 0 || strcmp(name, "B") == 0;
}

/*
  set up the type environment and add the one name that exists before
  the user's program starts. r0 is the synthetic input/output register, so later checks can treat it like any other int variable.
 */
void typeenv_init(TypeEnv *env) {
    if (env == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "null type env");
    }

    env->entries = malloc(sizeof(VarEntry) * TYPECHECKER_INITIAL_CAPACITY);
    if (env->entries == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "malloc failed");
    }

    env->count = 1;
    env->capacity = TYPECHECKER_INITIAL_CAPACITY;

    env->entries[0].name          = R0_SYNTHETIC_VARIABLE;
    env->entries[0].type          = TYPE_INT;
    env->entries[0].index         = 0;
    env->entries[0].alg_known     = false;
    env->entries[0].alg_value     = NULL;
    env->entries[0].required_order = 0; /* R0 is pre-assigned; no order constraint */
}

/*
    find a variable that has already been declared.
    this is deliberately just a linear scan because the spec says that is
    fine, and it keeps the first version easy to debug.
 */
VarEntry *typeenv_lookup(TypeEnv *env, const char *name) {
    if (env == NULL || name == NULL) {
        return NULL;
    }

    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->entries[i].name, name) == 0) {
            return &env->entries[i];
        }
    }

    return NULL;
}




// --------------- Helper for desugarer: -----------------------

void typeenv_declare_temp(TypeEnv *env, const char *name, TypeKind type) {
    // grow if needed
    if (env->count == env->capacity) {
        env->capacity *= 2;
        env->entries = realloc(env->entries, env->capacity * sizeof(VarEntry));
    }

    int idx = env->count++;
    VarEntry *e = &env->entries[idx];

    e->name           = typechecker_strdup(name);
    e->type           = type;
    e->index          = idx;
    e->alg_known      = false;
    e->alg_value      = NULL;
    e->required_order = 0; /* temporaries have no order constraint */
}
    //used to return idx but i don't think it's necessary
/*
    Return the stable small integer id for a variable name.
    Later passes can call this when they need array/bitset positions instead
    of the original string name. -1 means the name was not found.
 */
int typeenv_index(TypeEnv *env, const char *name) {
   VarEntry* entry = typeenv_lookup(env, name);
   if (entry == NULL) {
    return -1;
   }
   return entry->index;
}

/*
    release the dynamic array owned by the environment.
    entry 0 is _io, whose name points at a string literal, so only user-added
    names from index 1 onward are freed.
 */
void typeenv_free(TypeEnv *env) {
    if (env == NULL) {
        return;
    }

    for (int i = 1; i < env->count; i++) {
        free(env->entries[i].name);
        free(env->entries[i].alg_value);
    }

    free(env->entries);
    env->entries = NULL;
    env->count = 0;
    env->capacity = 0;
}

/*
    Add one new variable to the type environment.

    This is called when typechecking a let statement. It is responsible for:
    checking the name is new, checking any type-specific naming rules, growing
    the array if needed, and assigning the variable its stable id.
 */
bool typeenv_declare(TypeEnv *env, const char *name, TypeKind type, int line) {
    if (env == NULL || name == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "null type env declaration");
    }

    if (typeenv_lookup(env, name) != NULL) {
        /*
            one variable name should mean one thing for the whole program
         */
        die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, line,
            "variable '%s' already declared", name);
    }

    if (type == TYPE_ALG && is_forbidden_alg_name(name)) {
        /*
            alg variables cannot steal names like U/R/F because those already
            mean cube moves inside algorithm strings
         */
        die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, line,
            "alg variable cannot be named '%s'", name);
    }

    if (env->count == env->capacity) {
        /*
            TypeEnv is just a growable array: double the storage whenever the
            current array fills up, then continue appending entries.

            the same as resizing_array_list from lectures!
         */
        int new_capacity = env->capacity * 2; //maybe need a max(x, 1) here?
        VarEntry *new_entries = realloc(env->entries,
                                        sizeof(VarEntry) * new_capacity);
        if (new_entries == NULL) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "realloc failed");
        }

        env->entries = new_entries;
        env->capacity = new_capacity;
    }

    int index = env->count;
    /*
        The stable id is the slot number. Later passes can use this small int
        for arrays/bitsets instead of comparing variable-name strings.
     */
    env->entries[index].name           = typechecker_strdup(name);
    env->entries[index].type           = type;
    env->entries[index].index          = index;
    env->entries[index].alg_known      = false;
    env->entries[index].alg_value      = NULL;
    env->entries[index].required_order = 0; /* filled in by typecheck_statement for STMT_DECL */
    env->count++;

    return true;
}

/*
    The label set is a simple list of every label name in the program.
    We collect labels before checking statements so:

        goto done
        ...
        done:

    is allowed even though the target appears later in the file.
 */
void labelset_init(LabelSet *labels) {
    if (labels == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "null label set");
    }

    labels->labels = malloc(sizeof(char *) * TYPECHECKER_INITIAL_CAPACITY);
    if (labels->labels == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "malloc failed");
    }

    labels->count = 0;
    labels->capacity = TYPECHECKER_INITIAL_CAPACITY;
}

/*
    Free each copied label string, then the backing array itself.
 */
void labelset_free(LabelSet *labels) {
    if (labels == NULL) {
        return;
    }

    for (int i = 0; i < labels->count; i++) {
        free(labels->labels[i]);
    }

    free(labels->labels);
    labels->labels = NULL; //avoids accidentally damaging stuff
    labels->count = 0;
    labels->capacity = 0;
}

/*
    Linear scan is fine for this assignment. Labels are only checked during
    typechecking, not in a hot runtime loop.
 */
bool labelset_contains(LabelSet *labels, const char *name) {
    if (labels == NULL || name == NULL) {
        return false;
    }

    for (int i = 0; i < labels->count; i++) {
        if (strcmp(labels->labels[i], name) == 0) {
            return true;
        }
    }

    return false;
}

/*
    Add one label definition to the label set.

    This mirrors typeenv_declare, but for labels instead of variables. A label
    does not need a type or id here; all we need to know is whether a goto
    target exists and whether any label name was defined twice.
 */
bool labelset_add(LabelSet *labels, const char *name, int line) {
    if (labels == NULL || name == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "null label declaration");
    }

    if (labelset_contains(labels, name)) {
        /*
            If two labels have the same name, a goto would not have one clear
            destination
         */
        die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, line,
            "label '%s' has already been declared", name);
    }

    if (labels->count == labels->capacity) {
        int new_capacity = labels->capacity * 2;
        char **new_labels = realloc(labels->labels,
                                    sizeof(char *) * new_capacity);
        if (new_labels == NULL) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "realloc failed");
        }

        labels->labels = new_labels;
        labels->capacity = new_capacity;
    }

    labels->labels[labels->count] = typechecker_strdup(name);
    labels->count++;

    return true;
}

/*
    Forward declaration for the recursive label collector.
    Statement lists can contain statements that themselves contain more
    statement lists, so the two helpers call each other.
 */
static void collect_labels_from_statement(Statement *stmt, LabelSet *labels);

/*
    Walk a list of statements and collect any label definitions inside it.
    This helper is used for the top-level program body and for nested bodies.
 */
static void collect_labels_from_statements(Statement **statements, int count,
                                           LabelSet *labels) {
    for (int i = 0; i < count; i++) {
        collect_labels_from_statement(statements[i], labels);
    }
}

/*
    Look at one statement. If it is a label, add it. If it owns nested
    statements, recursively scan those nested statements too.
 */
static void collect_labels_from_statement(Statement *stmt, LabelSet *labels) {
    if (stmt == NULL) {
        return;
    }

    switch (stmt->kind) {
    case STMT_LABEL:
        labelset_add(labels, stmt->label_name, 0);
        break;

    case STMT_IF:
        if (stmt->if_stmt.if_then != NULL)
            collect_labels_from_statement(stmt->if_stmt.if_then, labels);
        if (stmt->if_stmt.if_else != NULL)
            collect_labels_from_statement(stmt->if_stmt.if_else, labels);
        break;

    case STMT_WHILE:
        if (stmt->while_stmt.while_body != NULL)
            collect_labels_from_statement(stmt->while_stmt.while_body, labels);
        break;

    case STMT_BLOCK:
        collect_labels_from_statements(stmt->block.block_stmts,
                                       stmt->block.block_count, labels);
        break;

    case STMT_DECL:
    case STMT_ASSIGN:
    case STMT_GOTO:
    case STMT_INPUT:
    case STMT_OUTPUT:
    case STMT_APPLY:
        break;
    }
}

/*
    Public entry point for the label pre-pass.

    After this runs, labels contains every legal goto target in the program.
    Actual goto statements are validated later during statement typechecking.
 */
void typecheck_collect_labels(ProgramAST *program, LabelSet *labels) {
    if (program == NULL || labels == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "null label collection");
    }

    collect_labels_from_statements(program->statements, program->count, labels);
}

/*
    Turn a TypeKind into text for error messages.
    This is not part of the public API; it just keeps diagnostics readable.
 */
static const char *typekind_name(TypeKind type) {
    switch (type) {
    case TYPE_INT:     return "int";
    case TYPE_ALG:     return "alg";
    case TYPE_BOOL:    return "bool";
    case TYPE_UNKNOWN: return "unknown";
    }
    return "unknown";
}

/*
    Check that a sub-expression has the type an operator needs.
    The expression has already been typechecked by the caller, so this helper
    only compares the resolved type and gives a useful error if it is wrong.
 */
static void require_expr_type(TypeKind actual, TypeKind expected,
                              const char *context) {
    if (actual != expected) {
        die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
            "%s expected %s but got %s",
            context, typekind_name(expected), typekind_name(actual));
    }
}

/*
    Typecheck one expression and write the answer into expr->resolved_type.

    Rules:
      - integer literals are int
      - algorithm literals are alg
      - variables take their type from the environment
      - +, -, unary - are int operations
      - ++ is alg concatenation
      - ord(a) takes an alg variable and produces an int
 */
TypeKind typecheck_expr(TypeEnv *env, Expr *expr) {
    if (env == NULL || expr == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "null expression");
    }

    switch (expr->kind) {
    case EXPR_INT_LIT:
        expr->type = TYPE_INT;
        return expr->type;

    case EXPR_ALG_LIT:
        expr->type = TYPE_ALG;
        return expr->type;

    case EXPR_VAR: {
        VarEntry *entry = typeenv_lookup(env, expr->var_name);
        if (entry == NULL) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "variable '%s' has not been declared", expr->var_name);
        }
        expr->type = entry->type;
        return expr->type;
    }

    case EXPR_ADD:
    case EXPR_SUB: {
        TypeKind left  = typecheck_expr(env, expr->bin_op.LHS);
        TypeKind right = typecheck_expr(env, expr->bin_op.RHS);
        require_expr_type(left,  TYPE_INT, "arithmetic left side");
        require_expr_type(right, TYPE_INT, "arithmetic right side");
        expr->type = TYPE_INT;
        return expr->type;
    }

    case EXPR_NEG: {
        TypeKind operand = typecheck_expr(env, expr->unary_op);
        require_expr_type(operand, TYPE_INT, "negation");
        expr->type = TYPE_INT;
        return expr->type;
    }

    case EXPR_CONCAT: {
        TypeKind left  = typecheck_expr(env, expr->bin_op.LHS);
        TypeKind right = typecheck_expr(env, expr->bin_op.RHS);
        require_expr_type(left,  TYPE_ALG, "concat left side");
        require_expr_type(right, TYPE_ALG, "concat right side");
        expr->type = TYPE_ALG;
        return expr->type;
    }

    case EXPR_ORD: {
        /* ord(a) — var_name holds the alg variable name in the current AST. */
        VarEntry *entry = typeenv_lookup(env, expr->var_name);
        if (entry == NULL) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "variable '%s' has not been declared", expr->var_name);
        }
        if (entry->type != TYPE_ALG) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "ord expected alg but got %s", typekind_name(entry->type));
        }
        if (!entry->alg_known) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                "alg variable '%s' is not compile-time known", expr->var_name);
        }

        expr->type = TYPE_INT;
        return expr->type;
    }

    case EXPR_EQ:
    case EXPR_LT:
    case EXPR_GT:
    case EXPR_LEQ:
    case EXPR_GEQ: {
        typecheck_expr(env, expr->bin_op.LHS);
        typecheck_expr(env, expr->bin_op.RHS);
        expr->type = TYPE_BOOL;
        return expr->type;
    }

    case EXPR_NOT: {
        typecheck_expr(env, expr->unary_op);
        expr->type = TYPE_BOOL;
        return expr->type;
    }

    case EXPR_SOLVED:
        expr->type = TYPE_BOOL;
        return expr->type;
    }

    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "unknown expression kind");
}

/*
    Typecheck one condition.

    Conditions do not produce a TypeKind, but they do have typed operands:
      - x = n compares an int variable with an int literal
      - x = y compares two int variables
      - solved[...] names concrete cube pieces
      - not recursively checks its operand
 */
/* In the current AST, conditions are Expr nodes: EXPR_EQ, EXPR_SOLVED, EXPR_NOT,
 * EXPR_LT, EXPR_GT, EXPR_LEQ, EXPR_GEQ. Both operands of a comparison are
 * themselves Expr nodes, already typechecked recursively. */
void typecheck_cond(TypeEnv *env, Expr *cond) {
    if (env == NULL || cond == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "null condition");
    }

    switch (cond->kind) {
    case EXPR_EQ:
    case EXPR_LT:
    case EXPR_GT:
    case EXPR_LEQ:
    case EXPR_GEQ: {
        TypeKind lhs = typecheck_expr(env, cond->bin_op.LHS);
        TypeKind rhs = typecheck_expr(env, cond->bin_op.RHS);
        require_expr_type(lhs, TYPE_INT, "comparison left side");
        require_expr_type(rhs, TYPE_INT, "comparison right side");
        break;
    }

    case EXPR_SOLVED:
        /* pieces is a PC_COUNT-terminated array of PieceLabel enums set by the
         * parser; validate each one is a real piece. */
        for (int i = 0; cond->pieces[i] != PC_COUNT; i++) {
            if ((int)cond->pieces[i] < 0 || (int)cond->pieces[i] >= PC_COUNT) {
                die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                    "unknown piece in solved[...]");
            }
        }
        break;

    case EXPR_NOT:
        typecheck_cond(env, cond->unary_op);
        break;

    default:
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
            "expression used as condition is not a valid condition kind");
    }
}

/*
    Return a newly allocated compile-time alg string for expr.

    The caller owns the returned string. NULL means the expression is not known
    at compile time; current language rules reject that at alg let/assign/apply
    sites, but keeping the helper shaped this way makes a future runtime alg
    extension local.
 */
static char *known_alg_expr(TypeEnv *env, Expr *expr) {
    if (env == NULL || expr == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "null alg expression");
    }

    switch (expr->kind) {
    case EXPR_ALG_LIT:
        /* alg_val is an Alg *; convert to a canonical single-spaced string. */
        return alg_to_string(expr->alg_val);

    case EXPR_VAR: {
        VarEntry *entry = typeenv_lookup(env, expr->var_name);
        if (entry == NULL || entry->type != TYPE_ALG || !entry->alg_known) {
            return NULL;
        }
        return typechecker_strdup(entry->alg_value);
    }

    case EXPR_CONCAT: {
        char *left  = known_alg_expr(env, expr->bin_op.LHS);
        char *right = known_alg_expr(env, expr->bin_op.RHS);
        if (left == NULL || right == NULL) {
            free(left);
            free(right);
            return NULL;
        }

        size_t llen = strlen(left);
        size_t rlen = strlen(right);
        bool needs_space = llen > 0 && rlen > 0;
        char *combined = malloc(llen + rlen + (needs_space ? 2 : 1));
        if (combined == NULL) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "malloc failed");
        }

        memcpy(combined, left, llen);
        size_t pos = llen;
        if (needs_space) combined[pos++] = ' ';
        memcpy(combined + pos, right, rlen);
        combined[pos + rlen] = '\0';

        free(left);
        free(right);
        return combined;
    }

    case EXPR_INT_LIT:
    case EXPR_ADD:
    case EXPR_SUB:
    case EXPR_NEG:
    case EXPR_ORD:
        return NULL;

    /* Condition kinds are not alg expressions. */
    case EXPR_SOLVED:
    case EXPR_NOT:
    case EXPR_EQ:
    case EXPR_LT:
    case EXPR_GT:
    case EXPR_LEQ:
    case EXPR_GEQ:
        return NULL;
    }

    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "unknown expression kind");
    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "unknown expression kind");
}

/*
    Record the compile-time alg value for a variable. This is used by later
    ord/apply/codegen work; in the current language every alg assignment must
    be known here.
 */
static void update_alg_value(TypeEnv *env, VarEntry *entry, Expr *rhs, int line) {
    char *value = known_alg_expr(env, rhs);
    if (value == NULL) {
        die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, line,
            "alg assignment requires compile-time-known rhs");
    }

    free(entry->alg_value);
    entry->alg_value = value;
    entry->alg_known = true;
}

static void assert_all_alg_values_known(TypeEnv *env) {
    for (int i = 0; i < env->count; i++) {
        if (env->entries[i].type == TYPE_ALG &&
            (!env->entries[i].alg_known || env->entries[i].alg_value == NULL)) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
                "alg variable '%s' is not compile-time known",
                env->entries[i].name);
        }
    }
}

static void typecheck_statements(TypeEnv *env, LabelSet *labels,
                                 Statement **statements, int count) {
    for (int i = 0; i < count; i++) {
        typecheck_statement(env, labels, statements[i]);
    }
}

/*
    Typecheck one statement.

    This enforces textual declaration order: a let name is added only after its
    initializer has been checked, so the initializer cannot refer to the name
    being declared.
 */
void typecheck_statement(TypeEnv *env, LabelSet *labels, Statement *stmt) {
    if (env == NULL || labels == NULL || stmt == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "null statement");
    }

    switch (stmt->kind) {
    case STMT_DECL: {
        const char  *name     = stmt->decl.decl_name;
        TypeKind     decltype = stmt->decl.decl_type;
        Expr        *init     = stmt->decl.decl_value;

        if (typeenv_lookup(env, name) != NULL) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "variable '%s' already declared", name);
        }

        if (decltype == TYPE_ALG && is_forbidden_alg_name(name)) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "alg variable cannot be named '%s'", name);
        }

        TypeKind init_type = typecheck_expr(env, init);
        if (init_type != decltype) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "let '%s' expected %s but got %s",
                name, typekind_name(decltype), typekind_name(init_type));
        }

        typeenv_declare(env, name, decltype, 0);

        VarEntry *entry = typeenv_lookup(env, name);
        if (decltype == TYPE_ALG) {
            update_alg_value(env, entry, init, 0);
        } else {
            /* int:N — record the required register order.
             * int_ord == 0 in the AST means the annotation was absent; treat as
             * no constraint (any register order ≥ 1 will do). */
            entry->required_order = (stmt->decl.int_ord > 0) ? stmt->decl.int_ord : 0;
        }

        break;
    }

    case STMT_ASSIGN: {
        VarEntry *entry = typeenv_lookup(env, stmt->assign.assign_name);
        if (entry == NULL) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "variable '%s' has not been declared", stmt->assign.assign_name);
        }

        TypeKind rhs_type = typecheck_expr(env, stmt->assign.assign_value);
        if (rhs_type != entry->type) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "assignment to '%s' expected %s but got %s",
                stmt->assign.assign_name, typekind_name(entry->type),
                typekind_name(rhs_type));
        }

        if (entry->type == TYPE_ALG) {
            update_alg_value(env, entry, stmt->assign.assign_value, 0);
        }

        break;
    }

    case STMT_IF:
        /* In the current AST, the condition is an Expr node. */
        typecheck_cond(env, stmt->if_stmt.if_cond);
        if (stmt->if_stmt.if_then != NULL)
            typecheck_statement(env, labels, stmt->if_stmt.if_then);
        if (stmt->if_stmt.if_else != NULL)
            typecheck_statement(env, labels, stmt->if_stmt.if_else);
        break;

    case STMT_WHILE:
        typecheck_cond(env, stmt->while_stmt.while_cond);
        if (stmt->while_stmt.while_body != NULL)
            typecheck_statement(env, labels, stmt->while_stmt.while_body);
        break;

    case STMT_GOTO:
        if (!labelset_contains(labels, stmt->goto_label)) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "label '%s' has not been declared", stmt->goto_label);
        }
        break;

    case STMT_LABEL:
    case STMT_INPUT:
        break;

    case STMT_OUTPUT:
        if (stmt->output_expr == NULL) {
            break;
        }
        if (stmt->output_expr->kind != EXPR_VAR) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "output expects a variable, or no operand for _io");
        }
        require_expr_type(typecheck_expr(env, stmt->output_expr), TYPE_INT, "output");
        break;

    case STMT_BLOCK:
        for (int i = 0; i < stmt->block.block_count; i++)
            typecheck_statement(env, labels, stmt->block.block_stmts[i]);
        break;

    case STMT_APPLY: {
        TypeKind alg_type = typecheck_expr(env, stmt->apply_expr);
        if (alg_type != TYPE_ALG) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "apply expected alg but got %s", typekind_name(alg_type));
        }

        char *value = known_alg_expr(env, stmt->apply_expr);
        if (value == NULL) {
            die(EXIT_CODE_TYPECHECK, STAGE_TYPECHECK, 0,
                "apply requires compile-time-known alg");
        }
        free(value);
        break;
    }

    default:
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "unknown statement kind");
    }
}

/*
    Whole-program typechecking entry point.

    The caller owns env because later passes need the populated TypeEnv. The
    label set is just a temporary pre-pass structure, so it is created and
    released here.
 */
void typecheck_program(ProgramAST *program, TypeEnv *env) {
    if (program == NULL || env == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0, "null program");
    }

    LabelSet labels;
    labelset_init(&labels);
    typecheck_collect_labels(program, &labels);
    typecheck_statements(env, &labels, program->statements, program->count);
    assert_all_alg_values_known(env);
    labelset_free(&labels);
}
