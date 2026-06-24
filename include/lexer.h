#ifndef ARMV8_37_LEXER_H
#define ARMV8_37_LEXER_H

#include <stdio.h>

#include "../include/alg.h"
#include "../include/piece.h"
#include "../include/util.h"

#define MAX_TOKEN_LENGTH 256 // Maximum length of a token, including identifiers

typedef enum {
    TOK_LET, // "let"
    TOK_INT, // "int"
    TOK_ALG, // "alg"
    TOK_IF, // "if"
    TOK_ELSE, // "else"
    TOK_WHILE, // "while"
    TOK_GOTO, // "goto"
    TOK_INPUT, // "input"
    TOK_OUTPUT, // "output"
    TOK_APPLY, // "apply"
    TOK_ORD, // "ord"
    TOK_SOLVED, // "solved"
    TOK_NOT, // "not"
    TOK_PIECE_LABEL, // a piece label, e.g. UFL, UR, DBR
    TOK_INT_LIT, // e.g. 42
    TOK_ALG_LIT, // e.g. "(R U R' U')2"
    TOK_STRING_LIT, // e.g. "Enter moves: "
    TOK_IDENT, // e.g x, myVar
    TOK_ASSIGN, // :=
    TOK_COLON, // :
    TOK_CONCAT, // ++
    TOK_PLUS, // +
    TOK_MINUS, // -
    TOK_EQ, // =
    TOK_LT, // <
    TOK_GT, // >
    TOK_LEQ, // <=
    TOK_GEQ, // >=
    TOK_LBRACE, // {
    TOK_RBRACE, // }
    TOK_LBRACKET, // (
    TOK_RBRACKET, // )
    TOK_L_SQ_BRACKET, // [
    TOK_R_SQ_BRACKET, // ]
    TOK_COMMA, // ,
    TOK_SEMICOLON, // ;
    TOK_EOF, // EOF
} TokenType;

typedef struct {
    TokenType type; // The type of the token
    union {
        int int_val; // Only valid for integer literals
        Alg alg_val; // Only valid for algorithms
        char *string_val; // Only valid for identifiers
        PieceLabel piece_label; // Only valid for piece labels
    }; // What value is represented by the token
    unsigned int line; // Useful for error messages
    unsigned int column; // Useful for error messages
} Token;

typedef struct {
    FILE *input;
    int current_line;
    int current_column;
    const struct SourceMap *source_map; // optional, NULL = identity mapping
    const char *source_filename;        // optional, NULL = no filename in errors
} Lexer;

// Frees a token and the text (if required)
void free_token(Token *token);

// Initialises a lexer
void init_lexer(Lexer *lexer, FILE *input);

// Frees a lexer
void free_lexer(Lexer *lexer);

// Produces the next token
Token *next_token(Lexer *lexer);

#endif //ARMV8_37_LEXER_H
