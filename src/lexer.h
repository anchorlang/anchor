#ifndef ANCC_LEXER_H
#define ANCC_LEXER_H

#include "arena.h"
#include "error.h"

#include <stddef.h>

typedef enum Token {
    // literals
    TOKEN_INTTEGER_LITERAL,
    TOKEN_STRING_LITERAL,

    // identifier
    TOKEN_IDENTIFIER,

    // keywords
    TOKEN_FUNC,
    TOKEN_RETURN,
    TOKEN_END,

    // arithmetic operators
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,

    // comparison operators
    TOKEN_EQUAL, // ==
    TOKEN_NOT_EQUAL, // !=
    TOKEN_LESS_THAN, // <
    TOKEN_GREATER_THAN, // >
    TOKEN_LESS_THAN_OR_EQUAL, // <=
    TOKEN_GREATER_THAN_OR_EQUAL, // >=

    // assignment
    TOKEN_ASSIGN, // =

    // punctuation
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_COLON,
    TOKEN_COMMA,
    TOKEN_DOT,

    // structural
    TOKEN_NEWLINE,

    // special
    TOKEN_END_OF_FILE,
    TOKEN_ERROR,
} Token;

typedef struct Tokens {
    Token* tokens;
    size_t count;
    size_t capacity;
} Tokens;

void lexer_tokenize(Arena* arena, Tokens* tokens, Errors* errors, char* buffer);

#endif
