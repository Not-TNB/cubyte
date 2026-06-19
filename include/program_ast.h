#ifndef ARMV8_37_AST_H
#define ARMV8_37_AST_H

#include "alg.h"
#include "piece.h"

typedef enum {
    TYPE_INT,
    TYPE_ALG,
    TYPE_BOOL,
    TYPE_UNKNOWN, // resolved later by typechecker (P2)
} TypeKind;

typedef enum {
    EXPR_INT_LIT, // integer literal
    EXPR_ALG_LIT, // alg literal string e.g. "R U R' U'"
    EXPR_SOLVED, // solved [pieces]
    EXPR_VAR, // variable reference
    EXPR_ADD, // e1 + e2
    EXPR_SUB, // e1 - e2
    EXPR_NEG, // -e
    EXPR_CONCAT, // e1 ++ e2  (alg concatenation)
    EXPR_ORD, // ord(var)
    EXPR_NOT, // not e
    EXPR_EQ, // e1 = e2
    EXPR_LT, // <
    EXPR_GT, // >
    EXPR_LEQ, // <=
    EXPR_GEQ, // >=
} ExprKind;

typedef struct Expr {
    ExprKind kind;
    TypeKind type;

    union {
        int int_val; // For integer literals
        Alg *alg_val; // For algorithm literals
        struct {
            struct Expr *LHS;
            struct Expr *RHS;
        } bin_op; // For binary operations
        char *var_name; // For variable references
        PieceLabel *pieces; // For solved
        struct Expr *unary_op; // For unary operations
    };
} Expr;

typedef enum {
    STMT_DECL,    // let [int|alg] name := expr ;
    STMT_ASSIGN,  // name := expr ;
    STMT_INPUT,   // input "prompt" ;
    STMT_OUTPUT,  // output [var] ;
    STMT_APPLY,   // apply expr ;
    STMT_IF,      // if expr { stmts } [else { stmts }]
    STMT_WHILE,   // while expr { stmts }
    STMT_GOTO,    // goto name ;
    STMT_LABEL,   // name :
    STMT_BLOCK,   // { stmts }
} StatementKind;

typedef struct Statement {
    StatementKind kind;

    union {
        // STMT_DECL: let [int|alg] name := expr ;
        struct {
            TypeKind decl_type;
            char *decl_name;
            Expr *decl_value;
            int int_ord; // For integer variables only
        } decl;

        // STMT_ASSIGN: name := expr ;
        struct {
            char *assign_name;
            Expr *assign_value;
        } assign;

        // STMT_INPUT: input "prompt" ;
        char *input_prompt;

        // STMT_OUTPUT: output [var] ;
        // NULL means the default I/O register R0.
        Expr *output_expr;

        // STMT_APPLY: apply expr ;
        Expr *apply_expr;

        // STMT_IF: if expr { stmts } [else { stmts }]
        struct {
            Expr *if_cond;
            struct Statement *if_then; // STMT_BLOCK
            struct Statement *if_else; // STMT_BLOCK or NULL
        } if_stmt;

        // STMT_WHILE: while expr { stmts }
        struct {
            Expr *while_cond;
            struct Statement *while_body; // STMT_BLOCK
        } while_stmt;

        // STMT_GOTO: goto name ;
        char *goto_label;

        // STMT_LABEL: name :
        char *label_name;

        // STMT_BLOCK: { stmts }
        struct {
            struct Statement **block_stmts;
            int block_count;
            int block_capacity;
        } block;
    };
} Statement;

typedef enum {
    COND_EQ_INT, // x = n
    COND_EQ_VAR, // x = y
    COND_SOLVED, // solved[p1, p2, ...]
    COND_NOT, // not <cond>
} CondKind;

typedef struct Cond {
    CondKind kind;
    int line;

    union {
        struct {
            char *var;
            int val;
            char *temp_name; // modification: we need 1 temp for this
        } eq_int;

        struct {
            char *lhs;
            char *rhs;
            char *temp_name_1; // modification: we need 2 temps for this
            char *temp_name_2;
        } eq_var; // temp_name filled by desugarer (P4); NULL until then
        struct {
            char **pieces;
            int count;
        } solved;

        struct Cond *operand; // NOT
    };
} Cond;

// Converts an expression into a condition.
// Handles: EXPR_SOLVED → COND_SOLVED
//          EXPR_NOT(inner) → COND_NOT wrapping convert_expr_into_cond(inner)
//          EXPR_EQ with one VAR side and one INT_LIT → COND_EQ_INT
//          EXPR_EQ with two VAR sides → COND_EQ_VAR
// Dies if you try to convert incompatible expression
Cond *convert_expr_into_cond(const Expr *expr);

// Frees a condition
void free_cond(Cond *cond);

typedef struct {
    Statement **statements;
    int count;
    int capacity;
} ProgramAST;

// Initialise a program
ProgramAST *init_program_ast();

// Parse a program
void parse_program(const char *input_file_name, ProgramAST *ast);

// Free the memory for a program. Safe to call with NULL.
void free_program_ast(ProgramAST *ast);

#endif //ARMV8_37_AST_H
