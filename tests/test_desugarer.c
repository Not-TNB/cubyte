#include "../include/desugarer.h"
#include "../include/program_ast.h"
#include "../include/typechecker.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *test_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    memcpy(copy, s, len);
    return copy;
}

static void check(bool ok, const char *message) {
    if (!ok) {
        fprintf(stderr, "desugarer test failed: %s\n", message);
        exit(1);
    }
}

static Expr *expr_int(int value) {
    Expr *expr = calloc(1, sizeof(Expr));
    check(expr != NULL, "allocate int expr");
    expr->kind = EXPR_INT_LIT;
    expr->resolved_type = TYPE_INT;
    expr->line = 1;
    expr->int_val = value;
    return expr;
}

static Expr *expr_var(const char *name) {
    Expr *expr = calloc(1, sizeof(Expr));
    check(expr != NULL, "allocate var expr");
    expr->kind = EXPR_VAR;
    expr->resolved_type = TYPE_INT;
    expr->line = 1;
    expr->var_name = test_strdup(name);
    return expr;
}

static Expr *expr_ord(const char *name) {
    Expr *expr = calloc(1, sizeof(Expr));
    check(expr != NULL, "allocate ord expr");
    expr->kind = EXPR_ORD;
    expr->resolved_type = TYPE_INT;
    expr->line = 1;
    expr->ord_var = test_strdup(name);
    return expr;
}

static Expr *expr_binop(ExprKind kind, Expr *left, Expr *right) {
    Expr *expr = calloc(1, sizeof(Expr));
    check(expr != NULL, "allocate binop expr");
    expr->kind = kind;
    expr->resolved_type = TYPE_INT;
    expr->line = 1;
    expr->binop.left = left;
    expr->binop.right = right;
    return expr;
}

static Statement *stmt_let_int(const char *name, Expr *init) {
    Statement *stmt = calloc(1, sizeof(Statement));
    check(stmt != NULL, "allocate let stmt");
    stmt->kind = STMT_LET;
    stmt->line = 1;
    stmt->let.name = test_strdup(name);
    stmt->let.type = TYPE_INT;
    stmt->let.init = init;
    return stmt;
}

static Statement *stmt_assign(const char *name, Expr *rhs) {
    Statement *stmt = calloc(1, sizeof(Statement));
    check(stmt != NULL, "allocate assign stmt");
    stmt->kind = STMT_ASSIGN;
    stmt->line = 1;
    stmt->assign.name = test_strdup(name);
    stmt->assign.rhs = rhs;
    return stmt;
}

static Cond *cond_eq_int(const char *name, int value) {
    Cond *cond = calloc(1, sizeof(Cond));
    check(cond != NULL, "allocate eq int cond");
    cond->kind = COND_EQ_INT;
    cond->line = 1;
    cond->eq_int.var = test_strdup(name);
    cond->eq_int.val = value;
    return cond;
}

static Cond *cond_not(Cond *operand) {
    Cond *cond = calloc(1, sizeof(Cond));
    check(cond != NULL, "allocate not cond");
    cond->kind = COND_NOT;
    cond->line = 1;
    cond->operand = operand;
    return cond;
}

static Statement *stmt_output(const char *label) {
    Statement *stmt = calloc(1, sizeof(Statement));
    check(stmt != NULL, "allocate output stmt");
    stmt->kind = STMT_OUTPUT;
    stmt->line = 1;
    stmt->output.label = test_strdup(label);
    return stmt;
}

static Statement *stmt_if(Cond *cond, Statement **then_body, int then_count,
                          Statement **else_body, int else_count) {
    Statement *stmt = calloc(1, sizeof(Statement));
    check(stmt != NULL, "allocate if stmt");
    stmt->kind = STMT_IF;
    stmt->line = 1;
    stmt->if_.cond = cond;
    stmt->if_.then_body = then_body;
    stmt->if_.then_count = then_count;
    stmt->if_.then_cap = then_count;
    stmt->if_.else_body = else_body;
    stmt->if_.else_count = else_count;
    stmt->if_.else_cap = else_count;
    return stmt;
}

static Statement *stmt_while(Cond *cond, Statement **body, int body_count) {
    Statement *stmt = calloc(1, sizeof(Statement));
    check(stmt != NULL, "allocate while stmt");
    stmt->kind = STMT_WHILE;
    stmt->line = 1;
    stmt->while_.cond = cond;
    stmt->while_.body = body;
    stmt->while_.body_count = body_count;
    stmt->while_.body_cap = body_count;
    return stmt;
}

static Statement **stmt_array(int count) {
    Statement **stmts = calloc((size_t)count, sizeof(Statement *));
    check(stmts != NULL, "allocate statement array");
    return stmts;
}

static void free_program_shell(ProgramAST *program) {
    free(program->statements);
    free(program);
}

static void declare_int(TypeEnv *env, const char *name) {
    check(typeenv_declare(env, name, TYPE_INT, 1), "declare int variable");
}

static void declare_known_alg(TypeEnv *env, const char *name,
                              const char *alg_value) {
    check(typeenv_declare(env, name, TYPE_ALG, 1), "declare alg variable");
    VarEntry *entry = typeenv_lookup(env, name);
    check(entry != NULL, "find declared alg variable");
    entry->alg_known = true;
    entry->alg_value = test_strdup(alg_value);
}

static void expect_var_expr(Expr *expr, const char *name, const char *context) {
    check(expr != NULL, context);
    check(expr->kind == EXPR_VAR, context);
    check(strcmp(expr->var_name, name) == 0, context);
}

static void expect_int_expr(Expr *expr, int value, const char *context) {
    check(expr != NULL, context);
    check(expr->kind == EXPR_INT_LIT, context);
    check(expr->int_val == value, context);
}

static void expect_let_int_value(Statement *stmt, const char *name, int value) {
    check(stmt->kind == STMT_LET, "expected let statement");
    check(strcmp(stmt->let.name, name) == 0, "expected let name");
    check(stmt->let.type == TYPE_INT, "expected int let type");
    expect_int_expr(stmt->let.init, value, "expected int let initializer");
}

static void expect_let_int_var(Statement *stmt, const char *name,
                               const char *init_name) {
    check(stmt->kind == STMT_LET, "expected let snapshot statement");
    check(strcmp(stmt->let.name, name) == 0, "expected let snapshot name");
    check(stmt->let.type == TYPE_INT, "expected int snapshot type");
    expect_var_expr(stmt->let.init, init_name, "expected snapshot initializer");
}

static void expect_assign_value(Statement *stmt, const char *name, int value) {
    check(stmt->kind == STMT_ASSIGN, "expected assignment statement");
    check(strcmp(stmt->assign.name, name) == 0, "expected assignment name");
    expect_int_expr(stmt->assign.rhs, value, "expected assignment literal");
}

static void expect_assign_op_value(Statement *stmt, const char *name,
                                   ExprKind op, int value) {
    check(stmt->kind == STMT_ASSIGN, "expected assignment op statement");
    check(strcmp(stmt->assign.name, name) == 0, "expected assignment op name");
    check(stmt->assign.rhs->kind == op, "expected assignment op kind");
    expect_var_expr(stmt->assign.rhs->binop.left, name, "expected lhs accumulator");
    expect_int_expr(stmt->assign.rhs->binop.right, value, "expected rhs literal");
}

static void expect_assign_op_var(Statement *stmt, const char *name, ExprKind op,
                                 const char *rhs_name) {
    check(stmt->kind == STMT_ASSIGN, "expected assignment op statement");
    check(strcmp(stmt->assign.name, name) == 0, "expected assignment op name");
    check(stmt->assign.rhs->kind == op, "expected assignment op kind");
    expect_var_expr(stmt->assign.rhs->binop.left, name, "expected lhs accumulator");
    expect_var_expr(stmt->assign.rhs->binop.right, rhs_name, "expected rhs variable");
}

static void test_int_let_literal_desugars_to_zero_plus_add(void) {
    TypeEnv env;
    typeenv_init(&env);
    declare_int(&env, "x");

    ProgramAST *program = init_program_ast();
    append_statement(program, stmt_let_int("x", expr_int(3)));

    ProgramAST *out = desugared_statement_ast(program, &env);

    check(out->count == 2, "literal let should produce two statements");
    expect_let_int_value(out->statements[0], "x", 0);
    expect_assign_op_value(out->statements[1], "x", EXPR_ADD, 3);

    free_program_ast(out);
    free_program_ast(program);
    typeenv_free(&env);
}

static void test_self_referential_assignment_uses_temp_snapshot(void) {
    TypeEnv env;
    typeenv_init(&env);
    declare_int(&env, "x");

    ProgramAST *program = init_program_ast();
    append_statement(program,
                     stmt_assign("x", expr_binop(EXPR_ADD, expr_var("x"), expr_int(2))));

    ProgramAST *out = desugared_statement_ast(program, &env);

    check(out->count == 4, "self assignment should snapshot, zero, and rebuild");
    expect_let_int_var(out->statements[0], "__t2", "x");
    expect_assign_value(out->statements[1], "x", 0);
    expect_assign_op_var(out->statements[2], "x", EXPR_ADD, "__t2");
    expect_assign_op_value(out->statements[3], "x", EXPR_ADD, 2);
    check(typeenv_lookup(&env, "__t2") != NULL, "temp should be declared");

    free_program_ast(out);
    free_program_ast(program);
    typeenv_free(&env);
}

static void test_assignment_flattens_subtraction_and_ord(void) {
    TypeEnv env;
    typeenv_init(&env);
    declare_int(&env, "x");
    declare_int(&env, "y");
    declare_known_alg(&env, "turn", "");

    ProgramAST *program = init_program_ast();
    Expr *sub = expr_binop(EXPR_SUB, expr_var("y"), expr_int(4));
    Expr *rhs = expr_binop(EXPR_ADD, sub, expr_ord("turn"));
    append_statement(program, stmt_assign("x", rhs));

    ProgramAST *out = desugared_statement_ast(program, &env);

    check(out->count == 4, "assignment should zero and emit one op per atom");
    expect_assign_value(out->statements[0], "x", 0);
    expect_assign_op_var(out->statements[1], "x", EXPR_ADD, "y");
    expect_assign_op_value(out->statements[2], "x", EXPR_SUB, 4);
    expect_assign_op_value(out->statements[3], "x", EXPR_ADD, 1);

    free_program_ast(out);
    free_program_ast(program);
    typeenv_free(&env);
}

static void test_if_body_is_desugared_recursively(void) {
    TypeEnv env;
    typeenv_init(&env);
    declare_int(&env, "x");
    declare_int(&env, "y");

    Statement **then_body = stmt_array(1);
    Statement *original_then_assign =
        stmt_assign("x", expr_binop(EXPR_ADD, expr_var("y"), expr_int(1)));
    then_body[0] = original_then_assign;

    Statement **else_body = stmt_array(1);
    else_body[0] = stmt_output("unchanged");

    ProgramAST *program = init_program_ast();
    append_statement(program,
                     stmt_if(cond_eq_int("x", 0), then_body, 1, else_body, 1));

    ProgramAST *out = desugared_statement_ast(program, &env);

    check(out->count == 1, "if should remain one outer statement");
    Statement *if_stmt = out->statements[0];
    check(if_stmt->kind == STMT_IF, "outer statement should still be if");
    check(if_stmt->if_.cond->kind == COND_EQ_INT, "if condition should be preserved");
    check(if_stmt->if_.then_count == 3, "if then body should be desugared");
    expect_assign_value(if_stmt->if_.then_body[0], "x", 0);
    expect_assign_op_var(if_stmt->if_.then_body[1], "x", EXPR_ADD, "y");
    expect_assign_op_value(if_stmt->if_.then_body[2], "x", EXPR_ADD, 1);
    check(if_stmt->if_.else_count == 1, "else body count should be preserved");
    check(if_stmt->if_.else_body[0]->kind == STMT_OUTPUT, "else output should stay");

    free_program_ast(out);
    free_statement(original_then_assign);
    free_program_shell(program);
    typeenv_free(&env);
}

static void test_while_body_is_desugared_and_not_condition_is_preserved(void) {
    TypeEnv env;
    typeenv_init(&env);
    declare_int(&env, "x");

    Statement **body = stmt_array(1);
    Statement *original_body_assign = stmt_assign("x", expr_int(1));
    body[0] = original_body_assign;

    ProgramAST *program = init_program_ast();
    append_statement(program, stmt_while(cond_not(cond_eq_int("x", 0)), body, 1));

    ProgramAST *out = desugared_statement_ast(program, &env);

    check(out->count == 1, "while should remain one outer statement");
    Statement *while_stmt = out->statements[0];
    check(while_stmt->kind == STMT_WHILE, "outer statement should still be while");
    check(while_stmt->while_.cond->kind == COND_NOT, "not condition should be preserved");
    check(while_stmt->while_.body_count == 2, "while body should be desugared");
    expect_assign_value(while_stmt->while_.body[0], "x", 0);
    expect_assign_op_value(while_stmt->while_.body[1], "x", EXPR_ADD, 1);

    free_program_ast(out);
    free_statement(original_body_assign);
    free_program_shell(program);
    typeenv_free(&env);
}

int main(void) {
    test_int_let_literal_desugars_to_zero_plus_add();
    test_self_referential_assignment_uses_temp_snapshot();
    test_assignment_flattens_subtraction_and_ord();
    test_if_body_is_desugared_recursively();
    test_while_body_is_desugared_and_not_condition_is_preserved();

    puts("desugarer tests passed");
    return 0;
}
