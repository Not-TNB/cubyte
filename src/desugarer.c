#include "desugarer.h"

#include "../include/alg.h"
#include "../include/cube.h"
#include "../include/util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This pass is written against the current ProgramAST layout:
 *   STMT_DECL / stmt->decl.*
 *   STMT_ASSIGN / stmt->assign.*
 *   STMT_BLOCK / stmt->block.*
 *   Expr::type, Expr::bin_op, Expr::unary_op
 *
 * desugared_statement_ast() transfers statement ownership from the input
 * ProgramAST into the returned ProgramAST. The input AST is left empty so it
 * may still be passed to free_program_ast() without double-freeing statements.
 */

static void turn_stmt_into_primitive(Statement *stmt, TypeEnv *type_env,
                                     ProgramAST *new_ast);

static char *desugarer_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    memcpy(copy, s, len);
    return copy;
}

static void append_statement_to_program(ProgramAST *ast, Statement *stmt) {
    if (ast == NULL || stmt == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
            "null statement append in desugarer");
    }

    if (ast->count == ast->capacity) {
        int new_capacity = next_cap(ast->capacity);
        Statement **new_statements =
            realloc(ast->statements, sizeof(Statement *) * (size_t)new_capacity);
        if (new_statements == NULL) {
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "realloc failed");
        }

        ast->statements = new_statements;
        ast->capacity = new_capacity;
    }

    ast->statements[ast->count++] = stmt;
}

static void free_expr_local(Expr *expr) {
    if (expr == NULL) {
        return;
    }

    switch (expr->kind) {
        case EXPR_INT_LIT:
            break;
        case EXPR_ALG_LIT:
            alg_free(expr->alg_val);
            free(expr->alg_val);
            break;
        case EXPR_SOLVED:
            free(expr->pieces);
            break;
        case EXPR_VAR:
        case EXPR_ORD:
            free(expr->var_name);
            break;
        case EXPR_ADD:
        case EXPR_SUB:
        case EXPR_CONCAT:
        case EXPR_EQ:
        case EXPR_LT:
        case EXPR_GT:
        case EXPR_LEQ:
        case EXPR_GEQ:
            free_expr_local(expr->bin_op.LHS);
            free_expr_local(expr->bin_op.RHS);
            break;
        case EXPR_NEG:
        case EXPR_NOT:
            free_expr_local(expr->unary_op);
            break;
    }

    free(expr);
}

static char *fresh_temp_name(TypeEnv *type_env) {
    int suffix = type_env->count;
    int needed = snprintf(NULL, 0, "__t%d", suffix);
    if (needed < 0) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
            "failed to format temp name");
    }

    char *name = malloc((size_t)needed + 1);
    if (name == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    snprintf(name, (size_t)needed + 1, "__t%d", suffix);
    return name;
}

static Expr *make_int_expr(int value) {
    Expr *expr = malloc(sizeof(Expr));
    if (expr == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    *expr = (Expr){
        .kind = EXPR_INT_LIT,
        .type = TYPE_INT,
        .int_val = value,
    };
    return expr;
}

static Expr *make_var_expr(const char *name) {
    Expr *expr = malloc(sizeof(Expr));
    if (expr == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    *expr = (Expr){
        .kind = EXPR_VAR,
        .type = TYPE_INT,
        .var_name = desugarer_strdup(name),
    };
    return expr;
}

static Expr *make_atom_expr(const char *rhs_name, int rhs_val) {
    if (rhs_name != NULL) {
        return make_var_expr(rhs_name);
    }

    return make_int_expr(rhs_val);
}

static Statement *make_int_decl_stmt(char *owned_name, Expr *owned_init) {
    Statement *stmt = malloc(sizeof(Statement));
    if (stmt == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    *stmt = (Statement){
        .kind = STMT_DECL,
        .decl = {
            .decl_type = TYPE_INT,
            .decl_name = owned_name,
            .decl_value = owned_init,
            .int_ord = 0,
        },
    };
    return stmt;
}

static void make_assign_stmt(ProgramAST *new_ast, const char *var_name,
                             bool add, const char *rhs_name, int rhs_val) {
    Expr *binop = malloc(sizeof(Expr));
    if (binop == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    *binop = (Expr){
        .kind = add ? EXPR_ADD : EXPR_SUB,
        .type = TYPE_INT,
        .bin_op = {
            .LHS = make_var_expr(var_name),
            .RHS = make_atom_expr(rhs_name, rhs_val),
        },
    };

    Statement *stmt = malloc(sizeof(Statement));
    if (stmt == NULL) {
        free_expr_local(binop);
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "malloc failed");
    }

    *stmt = (Statement){
        .kind = STMT_ASSIGN,
        .assign = {
            .assign_name = desugarer_strdup(var_name),
            .assign_value = binop,
        },
    };

    append_statement_to_program(new_ast, stmt);
}

static bool expr_contains_var(Expr *expr, const char *name) {
    if (expr == NULL) {
        return false;
    }

    switch (expr->kind) {
        case EXPR_VAR:
            return strcmp(expr->var_name, name) == 0;
        case EXPR_NEG:
            return expr_contains_var(expr->unary_op, name);
        case EXPR_ADD:
        case EXPR_SUB:
            return expr_contains_var(expr->bin_op.LHS, name) ||
                   expr_contains_var(expr->bin_op.RHS, name);
        case EXPR_INT_LIT:
        case EXPR_ALG_LIT:
        case EXPR_SOLVED:
        case EXPR_CONCAT:
        case EXPR_ORD:
        case EXPR_NOT:
        case EXPR_EQ:
        case EXPR_LT:
        case EXPR_GT:
        case EXPR_LEQ:
        case EXPR_GEQ:
            return false;
    }

    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "unknown expression kind");
}

static bool expr_is_var_named(const Expr *expr, const char *name) {
    return expr != NULL &&
           expr->kind == EXPR_VAR &&
           strcmp(expr->var_name, name) == 0;
}

static bool expr_is_codegen_atom(const Expr *expr) {
    return expr != NULL &&
           (expr->kind == EXPR_INT_LIT || expr->kind == EXPR_VAR);
}

static bool expr_is_primitive_self_update(const Expr *expr, const char *target) {
    if (expr == NULL || (expr->kind != EXPR_ADD && expr->kind != EXPR_SUB)) {
        return false;
    }

    return expr_is_var_named(expr->bin_op.LHS, target) &&
           expr_is_codegen_atom(expr->bin_op.RHS);
}

static void expr_substitute_var(Expr *expr, const char *old_name,
                                const char *new_name) {
    if (expr == NULL) {
        return;
    }

    switch (expr->kind) {
        case EXPR_VAR:
            if (strcmp(expr->var_name, old_name) == 0) {
                free(expr->var_name);
                expr->var_name = desugarer_strdup(new_name);
            }
            return;
        case EXPR_NEG:
            expr_substitute_var(expr->unary_op, old_name, new_name);
            return;
        case EXPR_ADD:
        case EXPR_SUB:
            expr_substitute_var(expr->bin_op.LHS, old_name, new_name);
            expr_substitute_var(expr->bin_op.RHS, old_name, new_name);
            return;
        case EXPR_INT_LIT:
        case EXPR_ALG_LIT:
        case EXPR_SOLVED:
        case EXPR_CONCAT:
        case EXPR_ORD:
        case EXPR_NOT:
        case EXPR_EQ:
        case EXPR_LT:
        case EXPR_GT:
        case EXPR_LEQ:
        case EXPR_GEQ:
            return;
    }

    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "unknown expression kind");
}

static int fold_ord_to_int(TypeEnv *env, const char *alg_var_name) {
    VarEntry *entry = typeenv_lookup(env, alg_var_name);
    if (entry == NULL || entry->type != TYPE_ALG || !entry->alg_known) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
            "ord() on unresolved alg variable '%s'", alg_var_name);
    }

    Alg parsed = {0};
    if (!alg_parse(entry->alg_value, &parsed)) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
            "invalid alg string for '%s'", alg_var_name);
    }

    int order = compute_order(&parsed);
    alg_free(&parsed);
    return order;
}

static void dfs_through_expression(Expr *expr, ProgramAST *new_ast, TypeEnv *env,
                                   bool add, const char *var_name) {
    if (expr == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
            "null expression in desugarer");
    }

    switch (expr->kind) {
        case EXPR_INT_LIT:
            make_assign_stmt(new_ast, var_name, add, NULL, expr->int_val);
            return;
        case EXPR_VAR:
            make_assign_stmt(new_ast, var_name, add, expr->var_name, 0);
            return;
        case EXPR_ORD: {
            int order = fold_ord_to_int(env, expr->var_name);
            make_assign_stmt(new_ast, var_name, add, NULL, order);
            return;
        }
        case EXPR_NEG:
            dfs_through_expression(expr->unary_op, new_ast, env, !add,
                                   var_name);
            return;
        case EXPR_ADD:
            dfs_through_expression(expr->bin_op.LHS, new_ast, env, add,
                                   var_name);
            dfs_through_expression(expr->bin_op.RHS, new_ast, env, add,
                                   var_name);
            return;
        case EXPR_SUB:
            dfs_through_expression(expr->bin_op.LHS, new_ast, env, add,
                                   var_name);
            dfs_through_expression(expr->bin_op.RHS, new_ast, env, !add,
                                   var_name);
            return;
        case EXPR_ALG_LIT:
        case EXPR_SOLVED:
        case EXPR_CONCAT:
        case EXPR_NOT:
        case EXPR_EQ:
        case EXPR_LT:
        case EXPR_GT:
        case EXPR_LEQ:
        case EXPR_GEQ:
            die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
                "non-int expression reached int desugarer");
    }

    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "unknown expression kind");
}

static void desugar_block(Statement *block, TypeEnv *type_env) {
    if (block == NULL) {
        return;
    }
    if (block->kind != STMT_BLOCK) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE,
            "expected block statement in desugarer");
    }

    ProgramAST *body_ast = init_program_ast();
    Statement **old_statements = block->block.block_stmts;
    int old_count = block->block.block_count;

    for (int i = 0; i < old_count; i++) {
        turn_stmt_into_primitive(old_statements[i], type_env, body_ast);
    }

    free(old_statements);
    block->block.block_stmts = body_ast->statements;
    block->block.block_count = body_ast->count;
    block->block.block_capacity = body_ast->capacity;
    free(body_ast);
}

static void turn_if_into_primitive(Statement *if_stmt, TypeEnv *type_env,
                                   ProgramAST *new_ast) {
    /*
     * Conditions are still stored as Expr in the current AST. Later stages can
     * call convert_expr_into_cond() when they need the Cond view; this pass
     * leaves if_cond itself intact and only desugars the nested statement
     * bodies.
     */
    desugar_block(if_stmt->if_stmt.if_then, type_env);
    desugar_block(if_stmt->if_stmt.if_else, type_env);
    append_statement_to_program(new_ast, if_stmt);
}

static void turn_while_into_primitive(Statement *while_stmt, TypeEnv *type_env,
                                      ProgramAST *new_ast) {
    desugar_block(while_stmt->while_stmt.while_body, type_env);
    append_statement_to_program(new_ast, while_stmt);
}

static void turn_int_decl_into_primitive(Statement *stmt, TypeEnv *type_env,
                                         ProgramAST *new_ast) {
    char *target = stmt->decl.decl_name;
    Expr *init = stmt->decl.decl_value;

    /*
     * Reuse the original declaration node as "let x : int := 0", then emit
     * additive primitive assignments for the original initializer.
     */
    stmt->decl.decl_value = make_int_expr(0);
    stmt->decl.int_ord = 0;
    append_statement_to_program(new_ast, stmt);

    dfs_through_expression(init, new_ast, type_env, true, target);
    free_expr_local(init);
}

static void turn_int_assign_into_primitive(Statement *stmt, TypeEnv *type_env,
                                           ProgramAST *new_ast) {
    char *target = stmt->assign.assign_name;
    Expr *rhs = stmt->assign.assign_value;

    if (expr_is_primitive_self_update(rhs, target)) {
        append_statement_to_program(new_ast, stmt);
        return;
    }

    /*
     * If the RHS reads the target, snapshot the old value first. This keeps
     * the subsequent zeroing assignment from destroying a value still needed by
     * the flattened additive steps.
     */
    if (expr_contains_var(rhs, target)) {
        char *temp = fresh_temp_name(type_env);
        typeenv_declare_temp(type_env, temp, TYPE_INT);

        append_statement_to_program(
            new_ast,
            make_int_decl_stmt(temp, make_var_expr(target))
        );

        expr_substitute_var(rhs, target, temp);
    }

    stmt->assign.assign_value = make_int_expr(0);
    append_statement_to_program(new_ast, stmt);

    dfs_through_expression(rhs, new_ast, type_env, true, target);
    free_expr_local(rhs);
}

static void turn_stmt_into_primitive(Statement *stmt, TypeEnv *type_env,
                                     ProgramAST *new_ast) {
    if (stmt == NULL) {
        return;
    }

    switch (stmt->kind) {
        case STMT_DECL:
            if (stmt->decl.decl_type == TYPE_INT) {
                turn_int_decl_into_primitive(stmt, type_env, new_ast);
            } else {
                append_statement_to_program(new_ast, stmt);
            }
            break;
        case STMT_ASSIGN: {
            VarEntry *entry = typeenv_lookup(type_env, stmt->assign.assign_name);
            if (entry != NULL && entry->type == TYPE_INT) {
                turn_int_assign_into_primitive(stmt, type_env, new_ast);
            } else {
                append_statement_to_program(new_ast, stmt);
            }
            break;
        }
        case STMT_IF:
            turn_if_into_primitive(stmt, type_env, new_ast);
            break;
        case STMT_WHILE:
            turn_while_into_primitive(stmt, type_env, new_ast);
            break;
        case STMT_BLOCK:
            desugar_block(stmt, type_env);
            append_statement_to_program(new_ast, stmt);
            break;
        case STMT_GOTO:
        case STMT_LABEL:
        case STMT_INPUT:
        case STMT_OUTPUT:
        case STMT_APPLY:
            append_statement_to_program(new_ast, stmt);
            break;
    }
}

ProgramAST *desugared_statement_ast(ProgramAST *program_ast, TypeEnv *type_env) {
    if (program_ast == NULL || type_env == NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, NO_SITE, "null desugarer input");
    }

    ProgramAST *new_ast = init_program_ast();
    Statement **old_statements = program_ast->statements;
    int old_count = program_ast->count;

    for (int i = 0; i < old_count; i++) {
        turn_stmt_into_primitive(old_statements[i], type_env, new_ast);
    }

    free(old_statements);
    program_ast->statements = NULL;
    program_ast->count = 0;
    program_ast->capacity = 0;

    return new_ast;
}
