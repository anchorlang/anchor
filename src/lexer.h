#ifndef ANCC_LEXER_H
#define ANCC_LEXER_H

#include "arena.h"
#include "error.h"

#include <stddef.h>

typedef enum TokenType {
    // literals
    TOKEN_INTEGER_LITERAL,
    TOKEN_FLOAT_LITERAL,
    TOKEN_STRING_LITERAL,

    // identifier
    TOKEN_IDENTIFIER,

    // keywords
    TOKEN_FUNC,
    TOKEN_RETURN,
    TOKEN_END,
    TOKEN_CONST,
    TOKEN_EXPORT,
    TOKEN_EXTERN,
    TOKEN_VAR,
    TOKEN_IF,
    TOKEN_ELSEIF,
    TOKEN_ELSE,
    TOKEN_STRUCT,
    TOKEN_INTERFACE,
    TOKEN_FOR,
    TOKEN_IN,
    TOKEN_UNTIL,
    TOKEN_STEP,
    TOKEN_WHILE,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_MATCH,
    TOKEN_CASE,
    TOKEN_ENUM,
    TOKEN_SELF,
    TOKEN_NULL,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_FROM,
    TOKEN_IMPORT,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_AS,

    // arithmetic operators
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_AMPERSAND, // &
    TOKEN_CARET, // ^

    // comparison operators
    TOKEN_EQUAL, // ==
    TOKEN_NOT_EQUAL, // !=
    TOKEN_LESS_THAN, // <
    TOKEN_GREATER_THAN, // >
    TOKEN_LESS_THAN_OR_EQUAL, // <=
    TOKEN_GREATER_THAN_OR_EQUAL, // >=

    // assignment
    TOKEN_ASSIGN, // =
    TOKEN_PLUS_ASSIGN, // +=
    TOKEN_MINUS_ASSIGN, // -=
    TOKEN_STAR_ASSIGN, // *=
    TOKEN_SLASH_ASSIGN, // /=

    // punctuation
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET,
    TOKEN_COLON,
    TOKEN_COMMA,
    TOKEN_DOT,

    // structural
    TOKEN_NEWLINE,

    // special
    TOKEN_END_OF_FILE,
    TOKEN_ERROR,
} TokenType;

typedef struct Token {
    TokenType type;
    char* value;
    size_t size;
    size_t offset;
    size_t line;
    size_t column;
} Token;

typedef struct Tokens {
    Token* tokens;
    size_t count;
    size_t capacity;
} Tokens;

void lexer_tokenize(Arena* arena, Tokens* tokens, Errors* errors, char* buffer, size_t buffer_size);

void lexer_print(Tokens* tokens);

#endif
