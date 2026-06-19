#include <stdlib.h>
#include <stdio.h>

#include "../include/program_ast.h"
#include "../include/lexer.h"
#include "../include/util.h"
#include "../include/preprocessor.h"

#include <stdbool.h>
#include <string.h>

#define INITIAL_CAPACITY 10

// strdup is POSIX, not standard C — use a small local version.
static char *program_ast_strdup(const char *s) {
    const size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        die(EXIT_FAILURE, "parser", 0, "Malloc failed");
    }
    memcpy(copy, s, len);
    return copy;
}

// Free the memory of an expression
static void free_expr(Expr *expr) {
    if (expr == NULL) return;
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
            free_expr(expr->bin_op.LHS);
            free_expr(expr->bin_op.RHS);
            break;
        case EXPR_NEG:
        case EXPR_NOT:
            free_expr(expr->unary_op);
            break;
    }

    free(expr);
}

// Free the memory of a statement
static void free_statement(Statement *statement) {
    if (statement == NULL) return;
    switch (statement->kind) {
        case STMT_DECL:
            free(statement->decl.decl_name);
            free_expr(statement->decl.decl_value);
            break;
        case STMT_ASSIGN:
            free(statement->assign.assign_name);
            free_expr(statement->assign.assign_value);
            break;
        case STMT_INPUT:
            free(statement->input_prompt);
            break;
        case STMT_OUTPUT:
            free_expr(statement->output_expr);
            break;
        case STMT_APPLY:
            free_expr(statement->apply_expr);
            break;
        case STMT_IF:
            free_expr(statement->if_stmt.if_cond);
            free_statement(statement->if_stmt.if_then);

            if (statement->if_stmt.if_else) {
                free_statement(statement->if_stmt.if_else);
            }
            break;
        case STMT_WHILE:
            free_expr(statement->while_stmt.while_cond);
            free_statement(statement->while_stmt.while_body);

            break;
        case STMT_GOTO:
            free(statement->goto_label);
            break;
        case STMT_LABEL:
            free(statement->label_name);
            break;
        case STMT_BLOCK:
            for (int i = 0; i < statement->block.block_count; i++) {
                free_statement(statement->block.block_stmts[i]);
            }
            free(statement->block.block_stmts);
            break;
    }

    free(statement);
}

// Initialise a program AST
ProgramAST *init_program_ast() {
    ProgramAST *ast = malloc(sizeof(ProgramAST));

    if (ast == NULL) {
        die(EXIT_FAILURE, "parser", 0, "Malloc failed");
    }

    Statement **statements = malloc(sizeof(Statement *) * INITIAL_CAPACITY);

    if (statements == NULL) {
        die(EXIT_FAILURE, "parser", 0, "Malloc failed");
    }

    ast->statements = statements;
    ast->capacity = INITIAL_CAPACITY;
    ast->count = 0;

    return ast;
}

// Appends a new statement to the program AST
static void append_statement(ProgramAST *ast, Statement *statement) {
    if (ast->count >= ast->capacity) {
        ast->capacity *= 2;
        Statement **tmp = realloc(ast->statements, sizeof(Statement *) * ast->capacity);
        if (tmp == NULL) die(EXIT_FAILURE, "parser", 0, "Malloc failed");
        ast->statements = tmp;
    }
    ast->statements[ast->count++] = statement;
}

// Converts an expression into a condition.
// Handles: EXPR_SOLVED → COND_SOLVED
//          EXPR_NOT(inner) → COND_NOT wrapping convert_expr_into_cond(inner)
//          EXPR_EQ with one VAR side and one INT_LIT → COND_EQ_INT
//          EXPR_EQ with two VAR sides → COND_EQ_VAR
// Dies if you try to convert incompatible expression
Cond *convert_expr_into_cond(const Expr *expr) {
    Cond *cond = malloc(sizeof(Cond));
    if (cond == NULL) {
        die(EXIT_FAILURE, "ast", 0, "Malloc failed");
    }

    switch (expr->kind) {

        case EXPR_SOLVED: {
            int count = 0;
            while (expr->pieces[count] != PC_COUNT) count++;

            char **piece_names = malloc(sizeof(char *) * count);
            if (piece_names == NULL) {
                free(cond);
                die(EXIT_FAILURE, "ast", 0, "Malloc failed");
            }
            for (int i = 0; i < count; i++) {
                piece_names[i] = NULL; // safe base for partial-free on die
            }
            for (int i = 0; i < count; i++) {
                const char *name = piece_to_string(expr->pieces[i]);
                if (name == NULL) {
                    for (int j = 0; j < i; j++) free(piece_names[j]);
                    free(piece_names);
                    free(cond);
                    die(EXIT_FAILURE, "ast", 0,
                        "convert_expr_into_cond: unknown piece label %d",
                        expr->pieces[i]);
                }
                piece_names[i] = program_ast_strdup(name);
            }

            cond->kind = COND_SOLVED;
            cond->solved.pieces = piece_names;
            cond->solved.count = count;
            return cond;
        }

        case EXPR_NOT: {
            // convert_expr_into_cond dies on failure so inner is always valid
            Cond *inner = convert_expr_into_cond(expr->unary_op);
            cond->kind = COND_NOT;
            cond->operand = inner;
            return cond;
        }

        case EXPR_EQ: {
            Expr *lhs = expr->bin_op.LHS;
            Expr *rhs = expr->bin_op.RHS;

            if (lhs->kind == EXPR_VAR && rhs->kind == EXPR_INT_LIT) {
                char *var = program_ast_strdup(lhs->var_name);
                cond->kind = COND_EQ_INT;
                cond->eq_int.var       = var;
                cond->eq_int.val       = rhs->int_val;
                cond->eq_int.temp_name = NULL;
                return cond;
            }
            if (lhs->kind == EXPR_INT_LIT && rhs->kind == EXPR_VAR) {
                char *var = program_ast_strdup(rhs->var_name);
                cond->kind = COND_EQ_INT;
                cond->eq_int.var       = var;
                cond->eq_int.val       = lhs->int_val;
                cond->eq_int.temp_name = NULL;
                return cond;
            }
            if (lhs->kind == EXPR_VAR && rhs->kind == EXPR_VAR) {
                char *l = program_ast_strdup(lhs->var_name);
                char *r = program_ast_strdup(rhs->var_name);
                cond->kind = COND_EQ_VAR;
                cond->eq_var.lhs         = l;
                cond->eq_var.rhs         = r;
                cond->eq_var.temp_name_1 = NULL;
                cond->eq_var.temp_name_2 = NULL;
                return cond;
            }

            free(cond);
            die(EXIT_FAILURE, "ast", 0,
                "convert_expr_into_cond: unsupported = operands "
                "(expected var/int or var/var)");
        }

        default:
            free(cond);
            die(EXIT_FAILURE, "ast", 0,
                "convert_expr_into_cond: cannot convert expression kind %d "
                "into a condition", expr->kind);
    }
}

// Frees a condition (recursively for COND_NOT).
void free_cond(Cond *cond) {
    if (cond == NULL) return;

    switch (cond->kind) {
        case COND_EQ_INT:
            free(cond->eq_int.var);
            free(cond->eq_int.temp_name); // safe: free(NULL) is a no-op
            break;

        case COND_EQ_VAR:
            free(cond->eq_var.lhs);
            free(cond->eq_var.rhs);
            free(cond->eq_var.temp_name_1);
            free(cond->eq_var.temp_name_2);
            break;

        case COND_SOLVED:
            for (int i = 0; i < cond->solved.count; i++) {
                free(cond->solved.pieces[i]);
            }
            free(cond->solved.pieces);
            break;

        case COND_NOT:
            free_cond(cond->operand);
            break;
    }

    free(cond);
}

// One-token lookahead buffer for the recursive-descent parser
static Token *peeked = NULL;
static bool has_peeked = false;

// Peeks at the next token without consuming it
static Token *peek_token(Lexer *lexer) {
    if (!has_peeked) {
        peeked = next_token(lexer);
        has_peeked = true;
    }
    return peeked;
}

// Consumes the currently peeked token, returning ownership to the caller
static Token *take_token(Lexer *lexer) {
    peek_token(lexer);
    Token *t = peeked;
    peeked = NULL;
    has_peeked = false;
    return t;
}

// Consumes a token, asserting it matches the expected kind. On mismatch,
// frees the token and returns false.
static bool expect(Lexer *lexer, const TokenType type, Token **out) {
    Token *t = take_token(lexer);
    if (t == NULL || t->type != type) {
        fprintf(stderr, "Parse error at line %u, column %u: expected token %d, got %d\n",
                t ? t->line : 0, t ? t->column : 0, type, t ? t->type : UINT32_MAX);
        free_token(t);
        return false;
    }

    *out = t;

    return true;
}

// Consumes a token, asserting it matches any of the expected kinds. On mismatch,
// frees the token and returns false.
static bool expect_any_of(Lexer *lexer, const TokenType *types, const int n_types, Token **out) {
    Token *t = take_token(lexer);

    if (t == NULL) {
        fprintf(stderr, "Parse error at line %u, column %u: unexpected %d\n",
                t ? t->line : 0, t ? t->column : 0, t ? t->type : UINT32_MAX);
        free_token(t);
        return false;
    }

    bool none_of_the_kinds = true;

    for (int i = 0; i < n_types; i++) {
        if (types[i] == t->type) {
            none_of_the_kinds = false;
        }
    }

    if (none_of_the_kinds) {
        fprintf(stderr, "Parse error at line %u, column %u: unexpected %d\n",
                t ? t->line : 0, t ? t->column : 0, t ? t->type : UINT32_MAX);
        free_token(t);
        return false;
    }

    *out = t;

    return true;
}

// Expression parsing:
//
// Precedence (lowest to highest):
//   1. =           (EXPR_EQ)
//   2. < > <= >=   (EXPR_LT/GT/LEQ/GEQ)
//   3. + - ++      (EXPR_ADD/SUB/CONCAT)
//   4. unary -     (EXPR_NEG)
//   5. unary not   (EXPR_NOT)
//   6. primary     (literals, var, solved [...], ord(var), parentheses)

static bool parse_expr(Lexer *lexer, Expr *expr);

static bool parse_eq(Lexer *lexer, Expr *expr);

static bool parse_rel(Lexer *lexer, Expr *expr);

static bool parse_add(Lexer *lexer, Expr *expr);

static bool parse_unary(Lexer *lexer, Expr *expr);

static bool parse_primary(Lexer *lexer, Expr *expr);

// Entry point: top of the expression precedence chain
static bool parse_expr(Lexer *lexer, Expr *expr) {
    return parse_eq(lexer, expr);
}

// Equality:  e = e
static bool parse_eq(Lexer *lexer, Expr *expr) {
    if (!parse_rel(lexer, expr)) return false;

    while (peek_token(lexer)->type == TOK_EQ) {
        free_token(take_token(lexer)); // consume '='

        Expr *rhs = malloc(sizeof(Expr));
        if (rhs == NULL) {
            die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
        }
        if (!parse_rel(lexer, rhs)) {
            free(rhs);
            return false;
        }

        Expr *lhs = malloc(sizeof(Expr));
        if (lhs == NULL) {
            die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
        }
        *lhs = *expr;

        expr->kind = EXPR_EQ;
        expr->bin_op.LHS = lhs;
        expr->bin_op.RHS = rhs;
    }
    return true;
}

// Relational:  e < e | e > e | e <= e | e >= e
static bool parse_rel(Lexer *lexer, Expr *expr) {
    if (!parse_add(lexer, expr)) return false;

    for (;;) {
        ExprKind k;
        switch (peek_token(lexer)->type) {
            case TOK_LT: k = EXPR_LT;
                break;
            case TOK_GT: k = EXPR_GT;
                break;
            case TOK_LEQ: k = EXPR_LEQ;
                break;
            case TOK_GEQ: k = EXPR_GEQ;
                break;
            default: return true;
        }
        free_token(take_token(lexer)); // consume the operator

        Expr *rhs = malloc(sizeof(Expr));
        if (rhs == NULL) {
            die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
        }
        if (!parse_add(lexer, rhs)) {
            free(rhs);
            return false;
        }

        Expr *lhs = malloc(sizeof(Expr));
        if (lhs == NULL) {
            die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
        }
        *lhs = *expr;

        expr->kind = k;
        expr->bin_op.LHS = lhs;
        expr->bin_op.RHS = rhs;
    }
}

// Additive:  e + e | e - e | e ++ e
static bool parse_add(Lexer *lexer, Expr *expr) {
    if (!parse_unary(lexer, expr)) return false;

    for (;;) {
        ExprKind k;
        switch (peek_token(lexer)->type) {
            case TOK_PLUS: k = EXPR_ADD;
                break;
            case TOK_MINUS: k = EXPR_SUB;
                break;
            case TOK_CONCAT: k = EXPR_CONCAT;
                break;
            default: return true;
        }
        free_token(take_token(lexer)); // consume the operator

        Expr *rhs = malloc(sizeof(Expr));
        if (rhs == NULL) {
            die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
        }
        if (!parse_unary(lexer, rhs)) {
            free(rhs);
            return false;
        }

        Expr *lhs = malloc(sizeof(Expr));
        if (lhs == NULL) {
            die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
        }
        *lhs = *expr;

        expr->kind = k;
        expr->bin_op.LHS = lhs;
        expr->bin_op.RHS = rhs;
    }
}

// Unary:  -e | not e
static bool parse_unary(Lexer *lexer, Expr *expr) {
    switch (peek_token(lexer)->type) {
        case TOK_MINUS: {
            free_token(take_token(lexer));
            Expr *inner = malloc(sizeof(Expr));
            if (inner == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }
            if (!parse_unary(lexer, inner)) {
                free(inner);
                return false;
            }
            expr->kind = EXPR_NEG;
            expr->unary_op = inner;
            return true;
        }
        case TOK_NOT: {
            free_token(take_token(lexer));
            Expr *inner = malloc(sizeof(Expr));
            if (inner == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }
            if (!parse_unary(lexer, inner)) {
                free(inner);
                return false;
            }
            expr->kind = EXPR_NOT;
            expr->unary_op = inner;
            return true;
        }
        default:
            return parse_primary(lexer, expr);
    }
}

// Primary: int literal, alg literal, identifier, (e), solved [...], ord(name)
static bool parse_primary(Lexer *lexer, Expr *expr) {
    Token *t = peek_token(lexer);

    switch (t->type) {
        case TOK_INT_LIT: {
            expr->kind = EXPR_INT_LIT;
            expr->int_val = t->int_val;
            take_token(lexer); // we've copied the value out
            free_token(t);
            return true;
        }

        case TOK_ALG_LIT: {
            // TOK_ALG_LIT carries an Alg by value in the lexer's union.
            // The AST stores an Alg*, so heap-allocate and copy.
            Alg *alg = malloc(sizeof(Alg));
            if (alg == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }
            *alg = t->alg_val;
            expr->kind = EXPR_ALG_LIT;
            expr->alg_val = alg;
            take_token(lexer);
            free_token(t);
            return true;
        }

        case TOK_IDENT: {
            char *name = program_ast_strdup(t->string_val);
            take_token(lexer);
            free_token(t);

            expr->kind = EXPR_VAR;
            expr->var_name = name;
            return true;
        }

        case TOK_ORD: {
            // ord ( name )
            take_token(lexer);
            free_token(t);
            Token *lparen;
            if (!expect(lexer, TOK_LBRACKET, &lparen)) return false;
            free_token(lparen);
            Token *id;
            if (!expect(lexer, TOK_IDENT, &id)) return false;
            char *name = id->string_val;
            id->string_val = NULL; // ownership transferred
            free_token(id);
            Token *rparen;
            if (!expect(lexer, TOK_RBRACKET, &rparen)) {
                free(name);
                return false;
            }
            free_token(rparen);

            expr->kind = EXPR_ORD;
            expr->var_name = name;
            return true;
        }

        case TOK_SOLVED: {
            // solved [ piece, piece, ... ]
            take_token(lexer);
            free_token(t);

            Token *lbr;
            if (!expect(lexer, TOK_L_SQ_BRACKET, &lbr)) return false;
            free_token(lbr);

            // Dynamic list of pieces; start with a small array and grow.
            int cap = 4, n = 0;
            PieceLabel *pieces = malloc(sizeof(PieceLabel) * cap);
            if (pieces == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }

            // Empty list is allowed: solved []
            if (peek_token(lexer)->type != TOK_R_SQ_BRACKET) {
                for (;;) {
                    Token *ptok;
                    if (!expect(lexer, TOK_PIECE_LABEL, &ptok)) {
                        free(pieces);
                        return false;
                    }
                    if (n >= cap) {
                        cap *= 2;
                        PieceLabel *tmp = realloc(pieces, sizeof(PieceLabel) * cap);
                        if (tmp == NULL) {
                            die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
                        }
                        pieces = tmp;
                    }
                    pieces[n++] = ptok->piece_label;
                    free_token(ptok);

                    if (peek_token(lexer)->type != TOK_COMMA) {
                        break;
                    }
                    free_token(take_token(lexer)); // consume and discard the comma
                }
            }

            // Sentinel-terminate the array so consumers can walk it without a count
            if (n >= cap) {
                PieceLabel *tmp = realloc(pieces, sizeof(PieceLabel) * (cap + 1));
                if (tmp == NULL) {
                    die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
                }
                pieces = tmp;
            }
            pieces[n] = PC_COUNT;

            Token *rbr;
            if (!expect(lexer, TOK_R_SQ_BRACKET, &rbr)) {
                free(pieces);
                return false;
            }
            free_token(rbr);

            expr->kind = EXPR_SOLVED;
            expr->pieces = pieces;
            return true;
        }

        case TOK_LBRACKET: {
            // ( e )
            take_token(lexer);
            free_token(t);
            if (!parse_expr(lexer, expr)) return false;
            Token *rparen;
            if (!expect(lexer, TOK_RBRACKET, &rparen)) return false;
            free_token(rparen);
            return true;
        }

        default: {
            fprintf(stderr, "Parse error at line %u, column %u: unexpected token in expression\n",
                    t->line, t->column);
            return false;
        }
    }
}

// Parses a statement
static bool parse_statement(Lexer *lexer, Statement *statement);

// Parses a `{ stmt; stmt; ... }` block, given a lexer
static bool parse_block(Lexer *lexer, Statement *block) {
    Token *lbr;
    if (!expect(lexer, TOK_LBRACE, &lbr)) return false;
    free_token(lbr);

    block->kind = STMT_BLOCK;
    block->block.block_stmts = malloc(sizeof(Statement *) * INITIAL_CAPACITY);
    if (block->block.block_stmts == NULL) {
        die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
    }
    block->block.block_capacity = INITIAL_CAPACITY;
    block->block.block_count = 0;

    while (peek_token(lexer)->type != TOK_RBRACE && peek_token(lexer)->type != TOK_EOF) {
        Statement *inner = malloc(sizeof(Statement));
        if (inner == NULL) {
            die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
        }

        if (!parse_statement(lexer, inner)) {
            free(inner);
            return false;
        }

        if (block->block.block_count >= block->block.block_capacity) {
            block->block.block_capacity *= 2;
            Statement **tmp = realloc(block->block.block_stmts,
                                      sizeof(Statement *) * block->block.block_capacity);
            if (tmp == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }
            block->block.block_stmts = tmp;
        }
        block->block.block_stmts[block->block.block_count++] = inner;
    }

    Token *rbr;
    if (!expect(lexer, TOK_RBRACE, &rbr)) {
        for (int i = 0; i < block->block.block_count; i++) {
            free_statement(block->block.block_stmts[i]);
        }
        free(block->block.block_stmts);
        return false;
    }
    free_token(rbr);

    return true;
}

// Parses a statement, given a lexer
static bool parse_statement(Lexer *lexer, Statement *statement) {
    // Consume any empty statements
    while (peek_token(lexer)->type == TOK_SEMICOLON) {
        free_token(take_token(lexer));
    }

    // Stop if we ran out of tokens after the empty statements
    if (peek_token(lexer)->type == TOK_EOF) {
        return false;
    }

    Token *t = peek_token(lexer);

    switch (t->type) {
        case TOK_LET: {
            // let [int : ord|alg] name := expr ;
            take_token(lexer);
            free_token(t);

            Token *type_tok;
            const TokenType types[] = {TOK_INT, TOK_ALG};

            if (!expect_any_of(lexer, types, 2, &type_tok)) {
                fprintf(stderr, "Parse error: expected type keyword (int or alg) after let\n");
                return false;
            }
            const TypeKind tk = type_tok->type == TOK_INT ? TYPE_INT : TYPE_ALG;
            free_token(type_tok);

            if (tk == TYPE_INT) {
                // Need to also check the register size
                Token *colon_tok;
                if (!expect(lexer, TOK_COLON, &colon_tok)) {
                    fprintf(stderr, "Parse error: expected : after let int\n");
                    free_token(colon_tok);
                }
                free_token(colon_tok);

                Token *reg_size_tok;
                if (!expect(lexer, TOK_INT_LIT, &reg_size_tok)) {
                    fprintf(stderr, "Parse error: expected register size after let int : \n");
                    free_token(reg_size_tok);
                    return false;
                }
                statement->decl.int_ord = reg_size_tok->int_val;
                free_token(reg_size_tok);
            }

            Token *name_tok;
            if (!expect(lexer, TOK_IDENT, &name_tok)) return false;
            char *name = name_tok->string_val;
            name_tok->string_val = NULL; // transfer ownership
            free_token(name_tok);

            Token *assign_tok;
            if (!expect(lexer, TOK_ASSIGN, &assign_tok)) {
                free(name);
                return false;
            }
            free_token(assign_tok);

            Expr *value = malloc(sizeof(Expr));
            if (value == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }
            if (!parse_expr(lexer, value)) {
                free(name);
                free(value);
                return false;
            }

            Token *semi;
            if (!expect(lexer, TOK_SEMICOLON, &semi)) {
                free(name);
                free(value);
                return false;
            }
            free_token(semi);

            statement->kind = STMT_DECL;
            statement->decl.decl_type = tk;
            statement->decl.decl_name = name;
            statement->decl.decl_value = value;
            return true;
        }

        case TOK_INPUT: {
            // input "prompt" ;
            take_token(lexer);
            free_token(t);

            Token *prompt_tok;
            const TokenType prompt_types[] = {TOK_STRING_LIT, TOK_ALG_LIT};
            if (!expect_any_of(lexer, prompt_types, 2, &prompt_tok)) return false;

            char *prompt = NULL;
            if (prompt_tok->type == TOK_STRING_LIT) {
                prompt = prompt_tok->string_val;
                prompt_tok->string_val = NULL;
            } else {
                prompt = alg_to_string(&prompt_tok->alg_val);
                alg_free(&prompt_tok->alg_val);
            }
            free_token(prompt_tok);

            Token *semi;
            if (!expect(lexer, TOK_SEMICOLON, &semi)) {
                free(prompt);
                return false;
            }
            free_token(semi);

            statement->kind = STMT_INPUT;
            statement->input_prompt = prompt;
            return true;
        }

        case TOK_OUTPUT: {
            // output [var] ;
            take_token(lexer);
            free_token(t);

            Expr *e = NULL;
            if (peek_token(lexer)->type != TOK_SEMICOLON) {
                e = malloc(sizeof(Expr));
                if (e == NULL) {
                    die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
                }
                if (!parse_expr(lexer, e)) {
                    free(e);
                    return false;
                }
            }

            Token *semi;
            if (!expect(lexer, TOK_SEMICOLON, &semi)) {
                free_expr(e);
                return false;
            }
            free_token(semi);

            statement->kind = STMT_OUTPUT;
            statement->output_expr = e;
            return true;
        }

        case TOK_APPLY: {
            // apply expr ;
            take_token(lexer);
            free_token(t);

            Expr *e = malloc(sizeof(Expr));
            if (e == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }
            if (!parse_expr(lexer, e)) {
                free(e);
                return false;
            }

            Token *semi;
            if (!expect(lexer, TOK_SEMICOLON, &semi)) {
                free(e);
                return false;
            }
            free_token(semi);

            statement->kind = STMT_APPLY;
            statement->apply_expr = e;
            return true;
        }

        case TOK_IF: {
            // if expr { stmts } [else { stmts }]
            take_token(lexer);
            free_token(t);

            Expr *cond = malloc(sizeof(Expr));
            if (cond == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }
            if (!parse_expr(lexer, cond)) {
                free(cond);
                return false;
            }

            Statement *then_body = malloc(sizeof(Statement));
            if (then_body == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }
            if (!parse_block(lexer, then_body)) {
                free(cond);
                free(then_body);
                return false;
            }

            Statement *else_body = NULL;
            if (peek_token(lexer)->type == TOK_ELSE) {
                free_token(take_token(lexer)); // consume the ELSE token
                else_body = malloc(sizeof(Statement));
                if (else_body == NULL) {
                    die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
                }
                if (!parse_block(lexer, else_body)) {
                    free(cond);
                    free(then_body);
                    free(else_body);
                    return false;
                }
            }

            statement->kind = STMT_IF;
            statement->if_stmt.if_cond = cond;
            statement->if_stmt.if_then = then_body;
            statement->if_stmt.if_else = else_body;
            return true;
        }

        case TOK_WHILE: {
            // while expr { stmts }
            take_token(lexer);
            free_token(t);

            Expr *cond = malloc(sizeof(Expr));
            if (cond == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }
            if (!parse_expr(lexer, cond)) {
                free(cond);
                return false;
            }

            Statement *body = malloc(sizeof(Statement));
            if (body == NULL) {
                die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
            }
            if (!parse_block(lexer, body)) {
                free(cond);
                free(body);
                return false;
            }

            statement->kind = STMT_WHILE;
            statement->while_stmt.while_cond = cond;
            statement->while_stmt.while_body = body;
            return true;
        }

        case TOK_GOTO: {
            // goto name ;
            take_token(lexer);
            free_token(t);

            Token *name_tok;
            if (!expect(lexer, TOK_IDENT, &name_tok)) return false;
            char *name = name_tok->string_val;
            name_tok->string_val = NULL;
            free_token(name_tok);

            Token *semi;
            if (!expect(lexer, TOK_SEMICOLON, &semi)) {
                free(name);
                return false;
            }
            free_token(semi);

            statement->kind = STMT_GOTO;
            statement->goto_label = name;
            return true;
        }

        case TOK_LBRACE: {
            // bare { stmts } — typically only valid as the body of if/while,
            // but we accept it as a statement too for symmetry
            return parse_block(lexer, statement);
        }

        case TOK_IDENT: {
            // Disambiguate name := (assign) vs name : (label)
            char *name = program_ast_strdup(t->string_val);
            take_token(lexer);
            free_token(t);

            const TokenType after = peek_token(lexer)->type;

            if (after == TOK_ASSIGN) {
                // name := expr ;
                free_token(take_token(lexer)); // consume :=

                Expr *value = malloc(sizeof(Expr));
                if (value == NULL) {
                    die(EXIT_FAILURE, "parser", lexer->current_line, "Malloc failed");
                }
                if (!parse_expr(lexer, value)) {
                    free(name);
                    free(value);
                    return false;
                }

                Token *semi;
                if (!expect(lexer, TOK_SEMICOLON, &semi)) {
                    free(name);
                    free(value);
                    return false;
                }
                free_token(semi);

                statement->kind = STMT_ASSIGN;
                statement->assign.assign_name = name;
                statement->assign.assign_value = value;
                return true;
            }

            if (after == TOK_COLON) {
                // name :
                free_token(take_token(lexer)); // consume :
                statement->kind = STMT_LABEL;
                statement->label_name = name;
                return true;
            }

            fprintf(stderr, "Parse error: expected ':=' or ':' after identifier '%s'\n", name);
            free(name);
            return false;
        }

        default: {
            fprintf(stderr, "Parse error at line %u, column %u: unexpected token at start of statement\n",
                    t->line, t->column);
            return false;
        }
    }
}

// Parse a program
void parse_program(const char *input_file_name, ProgramAST *ast) {
    preprocess(input_file_name);

    char *preprocessed_out_filename = malloc(strlen(input_file_name) + 20);
    if (preprocessed_out_filename == NULL) {
        die(EXIT_FAILURE, "parser", 0, "Malloc failed");
    }

    strcpy(preprocessed_out_filename, input_file_name);
    strcat(preprocessed_out_filename, "-pp.cbyte");

    FILE *input = fopen(preprocessed_out_filename, "r");

    if (input == NULL) {
        free(preprocessed_out_filename);
        die(EXIT_FAILURE, "parser", 0, "Failed to open file %s", input_file_name);
    }

    Lexer lexer;
    init_lexer(&lexer, input);

    while (peek_token(&lexer)->type != TOK_EOF) {
        Statement *statement = malloc(sizeof(Statement));
        if (statement == NULL) {
            die(EXIT_FAILURE, "parser", lexer.current_line, "Malloc failed");
        }

        if (parse_statement(&lexer, statement)) {
            append_statement(ast, statement);
        } else if (peek_token(&lexer)->type != TOK_EOF) {
            free_statement(statement);
            die(EXIT_FAILURE, "parser", lexer.current_line, "Parsing failed for statement");
        } else {
            free_statement(statement);
        }
    }

    free(preprocessed_out_filename);
    // Final EOF clean-up
    free_token(take_token(&lexer));

    fclose(input);
}

// Free the memory of a code
void free_program_ast(ProgramAST *ast) {
    if (ast == NULL) return;
    for (int i = 0; i < ast->count; i++) {
        free_statement(ast->statements[i]);
    }

    free(ast->statements);
    free(ast);
}
