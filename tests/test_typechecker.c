#include "../include/piece.h"
#include "../include/program_ast.h"
#include "../include/typechecker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(expr)                                                       \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "assertion failed at %s:%d: %s\n", __FILE__,      \
                    __LINE__, #expr);                                          \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_EQ_INT(actual, expected)                                         \
    do {                                                                       \
        int actual_value = (int)(actual);                                      \
        int expected_value = (int)(expected);                                  \
        if (actual_value != expected_value) {                                  \
            fprintf(stderr,                                                     \
                    "assertion failed at %s:%d: got %d, expected %d\n",        \
                    __FILE__, __LINE__, actual_value, expected_value);         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_STREQ(actual, expected)                                          \
    do {                                                                       \
        const char *actual_value = (actual);                                   \
        const char *expected_value = (expected);                               \
        if (actual_value == NULL || strcmp(actual_value, expected_value) != 0) {\
            fprintf(stderr,                                                     \
                    "assertion failed at %s:%d: got \"%s\", expected \"%s\"\n",\
                    __FILE__, __LINE__,                                        \
                    actual_value ? actual_value : "<null>", expected_value);   \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static char *xstrdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    ASSERT_TRUE(copy != NULL);
    memcpy(copy, s, len);
    return copy;
}

static Expr *expr_int(int value) {
    Expr *expr = calloc(1, sizeof(*expr));
    ASSERT_TRUE(expr != NULL);
    expr->kind = EXPR_INT_LIT;
    expr->int_val = value;
    return expr;
}

static Expr *expr_alg(const char *alg) {
    Expr *expr = calloc(1, sizeof(*expr));
    ASSERT_TRUE(expr != NULL);
    expr->kind = EXPR_ALG_LIT;
    expr->alg_str = xstrdup(alg);
    return expr;
}

static Expr *expr_var(const char *name) {
    Expr *expr = calloc(1, sizeof(*expr));
    ASSERT_TRUE(expr != NULL);
    expr->kind = EXPR_VAR;
    expr->var_name = xstrdup(name);
    return expr;
}

static Expr *expr_binop(ExprKind kind, Expr *left, Expr *right) {
    Expr *expr = calloc(1, sizeof(*expr));
    ASSERT_TRUE(expr != NULL);
    expr->kind = kind;
    expr->binop.left = left;
    expr->binop.right = right;
    return expr;
}

static Expr *expr_ord(const char *name) {
    Expr *expr = calloc(1, sizeof(*expr));
    ASSERT_TRUE(expr != NULL);
    expr->kind = EXPR_ORD;
    expr->ord_var = xstrdup(name);
    return expr;
}

static Cond *cond_eq_int(const char *name, int value) {
    Cond *cond = calloc(1, sizeof(*cond));
    ASSERT_TRUE(cond != NULL);
    cond->kind = COND_EQ_INT;
    cond->eq_int.var = xstrdup(name);
    cond->eq_int.val = value;
    return cond;
}

static Cond *cond_eq_var(const char *lhs, const char *rhs) {
    Cond *cond = calloc(1, sizeof(*cond));
    ASSERT_TRUE(cond != NULL);
    cond->kind = COND_EQ_VAR;
    cond->eq_var.lhs = xstrdup(lhs);
    cond->eq_var.rhs = xstrdup(rhs);
    return cond;
}

static Cond *cond_solved_one(const char *piece) {
    Cond *cond = calloc(1, sizeof(*cond));
    ASSERT_TRUE(cond != NULL);
    cond->kind = COND_SOLVED;
    cond->solved.pieces = calloc(1, sizeof(char *));
    ASSERT_TRUE(cond->solved.pieces != NULL);
    cond->solved.pieces[0] = xstrdup(piece);
    cond->solved.count = 1;
    return cond;
}

static Cond *cond_not(Cond *inner) {
    Cond *cond = calloc(1, sizeof(*cond));
    ASSERT_TRUE(cond != NULL);
    cond->kind = COND_NOT;
    cond->operand = inner;
    return cond;
}

static Statement *stmt_let(const char *name, TypeKind type, Expr *init) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    ASSERT_TRUE(stmt != NULL);
    stmt->kind = STMT_LET;
    stmt->let.name = xstrdup(name);
    stmt->let.type = type;
    stmt->let.init = init;
    return stmt;
}

static Statement *stmt_assign(const char *name, Expr *rhs) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    ASSERT_TRUE(stmt != NULL);
    stmt->kind = STMT_ASSIGN;
    stmt->assign.name = xstrdup(name);
    stmt->assign.rhs = rhs;
    return stmt;
}

static Statement *stmt_label(const char *name) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    ASSERT_TRUE(stmt != NULL);
    stmt->kind = STMT_LABEL;
    stmt->label.name = xstrdup(name);
    return stmt;
}

static Statement *stmt_goto(const char *target) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    ASSERT_TRUE(stmt != NULL);
    stmt->kind = STMT_GOTO;
    stmt->goto_.target = xstrdup(target);
    return stmt;
}

static Statement *stmt_apply(Expr *alg_expr) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    ASSERT_TRUE(stmt != NULL);
    stmt->kind = STMT_APPLY;
    stmt->apply.alg_expr = alg_expr;
    return stmt;
}

static Statement *stmt_if_empty(Cond *cond) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    ASSERT_TRUE(stmt != NULL);
    stmt->kind = STMT_IF;
    stmt->if_.cond = cond;
    return stmt;
}

static Statement *stmt_while_empty(Cond *cond) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    ASSERT_TRUE(stmt != NULL);
    stmt->kind = STMT_WHILE;
    stmt->while_.cond = cond;
    return stmt;
}

static void test_typeenv_basics(void) {
    TypeEnv env;
    typeenv_init(&env);

    ASSERT_EQ_INT(env.count, 1);
    ASSERT_EQ_INT(typeenv_index(&env, R0_SYNTHETIC_VARIABLE), 0);
    ASSERT_TRUE(typeenv_lookup(&env, "missing") == NULL);

    ASSERT_TRUE(typeenv_declare(&env, "x", TYPE_INT, 1));
    ASSERT_TRUE(typeenv_declare(&env, "moves", TYPE_ALG, 1));
    ASSERT_EQ_INT(typeenv_index(&env, "x"), 1);
    ASSERT_EQ_INT(typeenv_index(&env, "moves"), 2);
    ASSERT_EQ_INT(typeenv_lookup(&env, "x")->type, TYPE_INT);
    ASSERT_EQ_INT(typeenv_lookup(&env, "moves")->type, TYPE_ALG);

    typeenv_free(&env);
}

static void test_expr_and_cond_typechecking(void) {
    TypeEnv env;
    typeenv_init(&env);
    ASSERT_TRUE(typeenv_declare(&env, "x", TYPE_INT, 1));
    ASSERT_TRUE(typeenv_declare(&env, "y", TYPE_INT, 1));
    ASSERT_TRUE(typeenv_declare(&env, "moves", TYPE_ALG, 1));

    VarEntry *moves = typeenv_lookup(&env, "moves");
    moves->alg_known = true;
    moves->alg_value = xstrdup("R U");

    Expr *sum = expr_binop(EXPR_ADD, expr_var("x"), expr_int(7));
    ASSERT_EQ_INT(typecheck_expr(&env, sum), TYPE_INT);
    ASSERT_EQ_INT(sum->resolved_type, TYPE_INT);
    ASSERT_EQ_INT(sum->binop.left->resolved_type, TYPE_INT);
    free_expr(sum);

    Expr *concat = expr_binop(EXPR_CONCAT, expr_var("moves"), expr_alg("R'"));
    ASSERT_EQ_INT(typecheck_expr(&env, concat), TYPE_ALG);
    ASSERT_EQ_INT(concat->binop.left->resolved_type, TYPE_ALG);
    ASSERT_EQ_INT(concat->binop.right->resolved_type, TYPE_ALG);
    free_expr(concat);

    Expr *ord = expr_ord("moves");
    ASSERT_EQ_INT(typecheck_expr(&env, ord), TYPE_INT);
    free_expr(ord);

    Cond *eq_int = cond_eq_int("x", 0);
    typecheck_cond(&env, eq_int);
    free_cond(eq_int);

    Cond *eq_var = cond_eq_var("x", "y");
    typecheck_cond(&env, eq_var);
    free_cond(eq_var);

    Cond *solved = cond_not(cond_solved_one("UF"));
    typecheck_cond(&env, solved);
    free_cond(solved);

    typeenv_free(&env);
}

static void test_program_typecheck_populates_env_and_alg_values(void) {
    TypeEnv env;
    typeenv_init(&env);
    ProgramAST *program = init_program_ast();

    append_statement(program, stmt_goto("done"));
    append_statement(program, stmt_let("x", TYPE_INT, expr_int(3)));
    append_statement(program, stmt_let("y", TYPE_INT,
                                      expr_binop(EXPR_ADD, expr_var("x"),
                                                 expr_int(4))));
    append_statement(program, stmt_let("a", TYPE_ALG, expr_alg("R U")));
    append_statement(program, stmt_let("b", TYPE_ALG,
                                      expr_binop(EXPR_CONCAT, expr_var("a"),
                                                 expr_alg("R'"))));
    append_statement(program, stmt_let("k", TYPE_INT, expr_ord("b")));
    append_statement(program, stmt_assign("a",
                                         expr_binop(EXPR_CONCAT, expr_var("b"),
                                                    expr_alg("U'"))));
    append_statement(program, stmt_apply(expr_var("a")));
    append_statement(program, stmt_if_empty(cond_eq_var("x", "y")));
    append_statement(program, stmt_while_empty(cond_not(cond_solved_one("UR"))));
    append_statement(program, stmt_label("done"));

    typecheck_program(program, &env);

    ASSERT_EQ_INT(typeenv_lookup(&env, "x")->type, TYPE_INT);
    ASSERT_EQ_INT(typeenv_lookup(&env, "k")->type, TYPE_INT);
    ASSERT_EQ_INT(typeenv_lookup(&env, "a")->type, TYPE_ALG);
    ASSERT_EQ_INT(typeenv_lookup(&env, "b")->type, TYPE_ALG);
    ASSERT_TRUE(typeenv_lookup(&env, "a")->alg_known);
    ASSERT_TRUE(typeenv_lookup(&env, "b")->alg_known);
    ASSERT_STREQ(typeenv_lookup(&env, "b")->alg_value, "R U R'");
    ASSERT_STREQ(typeenv_lookup(&env, "a")->alg_value, "R U R' U'");

    free_program_ast(program);
    typeenv_free(&env);
}

static void test_label_collection_finds_nested_labels(void) {
    ProgramAST *program = init_program_ast();
    Statement *outer = stmt_if_empty(cond_solved_one("DF"));

    outer->if_.then_body = calloc(1, sizeof(Statement *));
    ASSERT_TRUE(outer->if_.then_body != NULL);
    outer->if_.then_body[0] = stmt_label("inside_then");
    outer->if_.then_count = 1;
    outer->if_.then_cap = 1;

    outer->if_.else_body = calloc(1, sizeof(Statement *));
    ASSERT_TRUE(outer->if_.else_body != NULL);
    outer->if_.else_body[0] = stmt_label("inside_else");
    outer->if_.else_count = 1;
    outer->if_.else_cap = 1;

    append_statement(program, outer);

    LabelSet labels;
    labelset_init(&labels);
    typecheck_collect_labels(program, &labels);
    ASSERT_TRUE(labelset_contains(&labels, "inside_then"));
    ASSERT_TRUE(labelset_contains(&labels, "inside_else"));
    ASSERT_TRUE(!labelset_contains(&labels, "missing"));

    labelset_free(&labels);
    free_program_ast(program);
}

int main(void) {
    test_typeenv_basics();
    test_expr_and_cond_typechecking();
    test_program_typecheck_populates_env_and_alg_values();
    test_label_collection_finds_nested_labels();
    puts("typechecker tests passed");
    return 0;
}
