#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "../include/lexer.h"
#include "../include/util.h"
#include "../include/piece.h"

#define MAX_TOKEN_ARR_LENGTH (MAX_TOKEN_LENGTH + 16)  // Just a good offset above
#define N_TOKENS 37                                   // The number of possible tokens

// strdup is POSIX, not standard C — use a small local version.
static char *lexer_strdup(const char *s) {
    const size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        // Best-effort: lexer doesn't carry a stage/line context here
        return NULL;
    }
    memcpy(copy, s, len);
    return copy;
}

// Frees a token and the text (if required)
void free_token(Token *token) {
    if (token == NULL) return;
    if (token->type == TOK_IDENT || token->type == TOK_STRING_LIT) {
        free(token->string_val);
    }
    free(token);
}

// Frees the lexer
void free_lexer(Lexer *lexer) {
    if (lexer == NULL) return;
    free(lexer);
}

// Initialises a lexer
void init_lexer(Lexer *lexer, FILE *input) {
    lexer->input = input;
    lexer->current_column = 1;
    lexer->current_line = 1;
}

// Checks if a character is a lone token (i.e. cannot continue a previous token)
bool is_lone_token(const int c) {
    if (isspace(c) || c == EOF) {
        return true;
    }

    switch (c) {
        case '{':
        case '}':
        case '(':
        case ')':
        case '[':
        case ']':
        case '<':
        case '>':
        case '=':
        case ':':
        case '+':
        case '-':
        case ',':
        case ';':
            return true;
        default:
            return false;
    }
}

// Produces the next token
Token *next_token(Lexer *lexer) {
    Token *token = malloc(sizeof(Token));

    if (token == NULL) {
        die(EXIT_FAILURE, "Tokenization", lexer->current_line, "Malloc failed");
    }

    char token_text[MAX_TOKEN_ARR_LENGTH] = {0}; // By 0 initialisation we won't forget null terminators

    int next_char = fgetc(lexer->input);

    // Skip any preceding whitespaces
    while (next_char != EOF && isspace(next_char)) {
        if (next_char == '\n' || next_char == '\f') {
            lexer->current_line++;
            lexer->current_column = 0;
        }

        if (next_char == '\r') {
            lexer->current_column = 0;
        }

        if (next_char == '\v') {
            lexer->current_line++;
            lexer->current_column--;
        }

        lexer->current_column++;

        next_char = fgetc(lexer->input);
    }

    // Update lexer column since we consumed a character
    lexer->current_column++;

    token->line = lexer->current_line;
    token->column = lexer->current_column;

    if (next_char == EOF) {
        // We have reached the end of the file
        token->type = TOK_EOF;
        return token;
    }

    // Initialise the text to the currently consumed char
    token_text[0] = next_char;

    // Check for one-character tokens
    switch (next_char) {
        case '{':
            token->type = TOK_LBRACE;
            return token;
        case '}':
            token->type = TOK_RBRACE;
            return token;
        case '(':
            token->type = TOK_LBRACKET;
            return token;
        case ')':
            token->type = TOK_RBRACKET;
            return token;
        case '[':
            token->type = TOK_L_SQ_BRACKET;
            return token;
        case ']':
            token->type = TOK_R_SQ_BRACKET;
            return token;
        case ',':
            token->type = TOK_COMMA;
            return token;
        case ';':
            token->type = TOK_SEMICOLON;
            return token;
        case '+': {
            // Need to check for if it is ++ or just +
            const int temp_next = fgetc(lexer->input);
            if (temp_next == '+') {
                token->type = TOK_CONCAT;
                return token;
            }
            token->type = TOK_PLUS;
            return token;
        }
        case '-':
            token->type = TOK_MINUS;
            return token;
        case ':': {
            // Need to check for if it is := or just :
            const int temp_next = fgetc(lexer->input);
            if (temp_next == '=') {
                token->type = TOK_ASSIGN;
                return token;
            }

            ungetc(temp_next, lexer->input);
            token->type = TOK_COLON;
            return token;
        }
        case '=':
            token->type = TOK_EQ;
            return token;
        case '<':
            token->type = TOK_LT;
            return token;
        case '>':
            token->type = TOK_GT;
            return token;
        default:
            break;
    }

    const bool is_alg_lit = next_char == '"';

    if (!is_alg_lit) {
        unsigned int token_length = 1;

        while (token_length < MAX_TOKEN_LENGTH) {
            next_char = fgetc(lexer->input);

            if (next_char == EOF || isspace(next_char) || is_lone_token(next_char)) {
                // We have reached a token separator, i.e. the end of the token text
                ungetc(next_char, lexer->input);
                break;
            }

            token_text[token_length] = next_char;
            token_length++;

            lexer->current_column++;

            // Check for two character tokens if the token length is 2
            if (token_length == 2) {
                if (strcmp(token_text, ":=") == 0) {
                    token->type = TOK_ASSIGN;
                    return token;
                }

                if (strcmp(token_text, ">=") == 0) {
                    token->type = TOK_GEQ;
                    return token;
                }

                if (strcmp(token_text, "<=") == 0) {
                    token->type = TOK_LEQ;
                    return token;
                }

                if (strcmp(token_text, "++") == 0) {
                    token->type = TOK_CONCAT;
                    return token;
                }
            }
        }

        if (token_text[MAX_TOKEN_LENGTH] != '\0') {
            // The token is too long
            die(EXIT_FAILURE, "Tokenization", lexer->current_line, "Overly long token at column %d: %s\n", token->column, token_text);
        }

        // Check if the token is a keyword
        if (strcmp(token_text, "let") == 0) {
            token->type = TOK_LET;
            return token;
        }

        if (strcmp(token_text, "int") == 0) {
            token->type = TOK_INT;
            return token;
        }

        if (strcmp(token_text, "alg") == 0) {
            token->type = TOK_ALG;
            return token;
        }

        if (strcmp(token_text, "if") == 0) {
            token->type = TOK_IF;
            return token;
        }

        if (strcmp(token_text, "else") == 0) {
            token->type = TOK_ELSE;
            return token;
        }

        if (strcmp(token_text, "while") == 0) {
            token->type = TOK_WHILE;
            return token;
        }

        if (strcmp(token_text, "goto") == 0) {
            token->type = TOK_GOTO;
            return token;
        }

        if (strcmp(token_text, "input") == 0) {
            token->type = TOK_INPUT;
            return token;
        }

        if (strcmp(token_text, "output") == 0) {
            token->type = TOK_OUTPUT;
            return token;
        }

        if (strcmp(token_text, "apply") == 0) {
            token->type = TOK_APPLY;
            return token;
        }

        if (strcmp(token_text, "ord") == 0) {
            token->type = TOK_ORD;
            return token;
        }

        if (strcmp(token_text, "solved") == 0) {
            token->type = TOK_SOLVED;
            return token;
        }

        if (strcmp(token_text, "not") == 0) {
            token->type = TOK_NOT;
            return token;
        }

        // Check for piece label (e.g. UFL, UR, DBR)
        const int piece_idx = piece_from_string(token_text);
        if (piece_idx >= 0) {
            token->type = TOK_PIECE_LABEL;
            token->piece_label = (PieceLabel) piece_idx;
            return token;
        }

        // Check for int literal
        bool all_digits = true;

        for (unsigned int i = 0; i < token_length; i++) {
            if (!isdigit(token_text[i])) {
                all_digits = false;
                break;
            }
        }

        if (all_digits) {
            token->type = TOK_INT_LIT;
            token->int_val = atoi(token_text);
            return token;
        }

        // Check for identifier
        regex_t ident_regex;

        regcomp(&ident_regex, "^[a-zA-Z_][a-zA-Z_0-9]*$", REG_EXTENDED | REG_NOSUB);

        if (regexec(&ident_regex, token_text, 0, NULL, 0) == 0) {
            token->type = TOK_IDENT;
            token->string_val = lexer_strdup(token_text);
            regfree(&ident_regex);
            return token;
        }

        regfree(&ident_regex);
    }

    // Check for quoted alg literal, e.g. "(R U R' U')2"
    if (is_alg_lit) {
        unsigned int token_length = 1;

        while (token_length < MAX_TOKEN_LENGTH) {
            next_char = fgetc(lexer->input);

            if (next_char == EOF) {
                die(EXIT_FAILURE,  "tokenizer", token->line, "Unclosed alg literal at column %d: %s\n", token->column, token_text);
            }

            if (next_char == '\n' || next_char == '\f' || next_char == '\v') {
                die(EXIT_FAILURE,  "tokenizer", token->line, "Unclosed alg literal at column %d: %s\n", token->column, token_text);
            }

            if (next_char == '\r') {
                lexer->current_column = 0;
            }

            lexer->current_column++;

            token_text[token_length] = next_char;
            token_length++;

            if (next_char == '"') {
                break;
            }
        }

        if (token_text[MAX_TOKEN_LENGTH] != '\0') {
            // The token is too long
            die(EXIT_FAILURE, "tokenizer", lexer->current_line, "Overly long alg literal at column %d: %s\n", token->column, token_text);
        }

        if (token_text[token_length - 1] != '"') {
            die(EXIT_FAILURE, "tokenizer", token->line, "Unclosed alg literal at column %d: %s\n", token->column, token_text);
        }

        // Strip the surrounding quotes before handing the text to alg_parse
        token_text[token_length - 1] = '\0';

        Alg alg = {0};
        if (alg_parse(token_text + 1, &alg)) {
            token->type = TOK_ALG_LIT;
            token->alg_val = alg;
            return token;
        }

        token->type = TOK_STRING_LIT;
        token->string_val = lexer_strdup(token_text + 1);
        if (token->string_val == NULL) {
            die(EXIT_FAILURE, "Tokenization", lexer->current_line, "Malloc failed");
        }
        return token;
    }

    // Otherwise we have a malformed token
    die(EXIT_FAILURE, "tokenizer", token->line, "Malformed token at column %d: %s\n", token->column, token_text);
}
