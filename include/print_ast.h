#ifndef PRINT_AST_H
#define PRINT_AST_H

#include "./program_ast.h"
#include "../include/alg.h"
#include "../include/piece.h"
#include "../include/util.h"

void pretty_print_expr(const Expr *expr);
void pretty_print_stmt(const Statement *stmt, int indent);

#endif