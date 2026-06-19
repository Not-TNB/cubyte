#include <stdlib.h>
#include <stdio.h>
#include "../include/print_ast.h"

// Forward declarations for mutually recursive printers
void pretty_print_expr(const Expr *expr);
void pretty_print_stmt(const Statement *stmt, int indent);

// Print an expression with minimal (but readable) parentheses
void pretty_print_expr(const Expr *expr) {
    if (expr == NULL) {
        return;
    }

    switch (expr->kind) {
        case EXPR_INT_LIT:
            printf("%d", expr->int_val);
            break;

        case EXPR_ALG_LIT: {
            char *s = alg_to_string(expr->alg_val);
            if (s != NULL) {
                printf("\"%s\"", s);
                free(s);
            } else {
                printf("\"\"");
            }
            break;
        }

        case EXPR_VAR:
            printf("%s", expr->var_name);
            break;

        case EXPR_ORD:
            printf("ord(%s)", expr->var_name);
            break;

        case EXPR_SOLVED: {
            printf("solved [");
            for (int i = 0; expr->pieces[i] != PC_COUNT; i++) {
                if (i > 0) printf(", ");
                printf("%s", piece_to_string(expr->pieces[i]));
            }
            printf("]");
            break;
        }

        case EXPR_NEG:
            printf("-");
            pretty_print_expr(expr->unary_op);
            break;

        case EXPR_NOT:
            printf("not ");
            pretty_print_expr(expr->unary_op);
            break;

        case EXPR_ADD:
            printf("(");
            pretty_print_expr(expr->bin_op.LHS);
            printf(" + ");
            pretty_print_expr(expr->bin_op.RHS);
            printf(")");
            break;

        case EXPR_SUB:
            printf("(");
            pretty_print_expr(expr->bin_op.LHS);
            printf(" - ");
            pretty_print_expr(expr->bin_op.RHS);
            printf(")");
            break;

        case EXPR_CONCAT:
            printf("(");
            pretty_print_expr(expr->bin_op.LHS);
            printf(" ++ ");
            pretty_print_expr(expr->bin_op.RHS);
            printf(")");
            break;

        case EXPR_EQ:
            printf("(");
            pretty_print_expr(expr->bin_op.LHS);
            printf(" = ");
            pretty_print_expr(expr->bin_op.RHS);
            printf(")");
            break;

        case EXPR_LT:
            printf("(");
            pretty_print_expr(expr->bin_op.LHS);
            printf(" < ");
            pretty_print_expr(expr->bin_op.RHS);
            printf(")");
            break;

        case EXPR_GT:
            printf("(");
            pretty_print_expr(expr->bin_op.LHS);
            printf(" > ");
            pretty_print_expr(expr->bin_op.RHS);
            printf(")");
            break;

        case EXPR_LEQ:
            printf("(");
            pretty_print_expr(expr->bin_op.LHS);
            printf(" <= ");
            pretty_print_expr(expr->bin_op.RHS);
            printf(")");
            break;

        case EXPR_GEQ:
            printf("(");
            pretty_print_expr(expr->bin_op.LHS);
            printf(" >= ");
            pretty_print_expr(expr->bin_op.RHS);
            printf(")");
            break;
    }
}

// Indent helper: print 2 spaces per level
static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

// Print a single statement. `indent` controls leading whitespace.
void pretty_print_stmt(const Statement *stmt, int indent) {
    if (stmt == NULL) {
        return;
    }

    switch (stmt->kind) {
        case STMT_DECL:
            print_indent(indent);
            if (stmt->decl.decl_type == TYPE_INT) {
                printf("let int : %d %s := ", stmt->decl.int_ord, stmt->decl.decl_name);
            } else {
                printf("let alg %s := ", stmt->decl.decl_name);
            }
            pretty_print_expr(stmt->decl.decl_value);
            printf(";\n");
            break;

        case STMT_ASSIGN:
            print_indent(indent);
            printf("%s := ", stmt->assign.assign_name);
            pretty_print_expr(stmt->assign.assign_value);
            printf(";\n");
            break;

        case STMT_INPUT:
            print_indent(indent);
            printf("input \"%s\";\n", stmt->input_prompt);
            break;

        case STMT_OUTPUT:
            print_indent(indent);
            if (stmt->output_expr == NULL) {
                printf("output;\n");
            } else {
                printf("output ");
                pretty_print_expr(stmt->output_expr);
                printf(";\n");
            }
            break;

        case STMT_APPLY:
            print_indent(indent);
            printf("apply ");
            pretty_print_expr(stmt->apply_expr);
            printf(";\n");
            break;

        case STMT_IF:
            print_indent(indent);
            printf("if ");
            pretty_print_expr(stmt->if_stmt.if_cond);
            printf(" {\n");
            pretty_print_stmt(stmt->if_stmt.if_then, indent + 1);
            print_indent(indent);
            printf("}");
            if (stmt->if_stmt.if_else != NULL) {
                printf(" else {\n");
                pretty_print_stmt(stmt->if_stmt.if_else, indent + 1);
                print_indent(indent);
                printf("}");
            }
            printf("\n");
            break;

        case STMT_WHILE:
            print_indent(indent);
            printf("while ");
            pretty_print_expr(stmt->while_stmt.while_cond);
            printf(" {\n");
            pretty_print_stmt(stmt->while_stmt.while_body, indent + 1);
            print_indent(indent);
            printf("}\n");
            break;

        case STMT_GOTO:
            print_indent(indent);
            printf("goto %s;\n", stmt->goto_label);
            break;

        case STMT_LABEL:
            print_indent(indent);
            printf("%s:\n", stmt->label_name);
            break;

        case STMT_BLOCK:
            for (int i = 0; i < stmt->block.block_count; i++) {
                pretty_print_stmt(stmt->block.block_stmts[i], indent);
            }
            break;
    }
}

// Pretty-print an entire program AST to stdout
void pretty_print(ProgramAST *ast) {
    if (ast == NULL) {
        return;
    }
    for (int i = 0; i < ast->count; i++) {
        pretty_print_stmt(ast->statements[i], 0);
    }
}
