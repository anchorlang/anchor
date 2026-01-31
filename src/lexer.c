#include "lexer.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

typedef struct Lexer {
    Arena* arena;
    Tokens* tokens;
    Errors* errors;
    char* start;
    char* current;
    char* token_start;
    size_t line;
    size_t column;
    size_t token_line;
    size_t token_column;
} Lexer;

static void tokens_push(Lexer* lexer, TokenType type) {
    Tokens* tokens = lexer->tokens;

    if (tokens->count >= tokens->capacity) {
        size_t new_capacity = tokens->capacity * 2;
        Token* new_tokens = arena_alloc(lexer->arena, new_capacity * sizeof(Token));
        memcpy(new_tokens, tokens->tokens, tokens->count * sizeof(Token));
        tokens->tokens = new_tokens;
        tokens->capacity = new_capacity;
    }

    Token* token = &tokens->tokens[tokens->count];
    token->type = type;
    token->value = lexer->token_start;
    token->size = (size_t)(lexer->current - lexer->token_start);
    token->offset = (size_t)(lexer->token_start - lexer->start);
    token->line = lexer->token_line;
    token->column = lexer->token_column;
    tokens->count++;
}

static char lexer_peek(Lexer* lexer) {
    return *lexer->current;
}

static char lexer_advance(Lexer* lexer) {
    char c = *lexer->current;
    lexer->current++;
    lexer->column++;
    return c;
}

static bool lexer_match(Lexer* lexer, char expected) {
    if (*lexer->current == expected) {
        lexer->current++;
        lexer->column++;
        return true;
    }
    return false;
}

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static void lexer_read_identifier_or_keyword(Lexer* lexer) {
    while (is_alnum(lexer_peek(lexer))) {
        lexer_advance(lexer);
    }

    size_t length = (size_t)(lexer->current - lexer->token_start);
    char* start = lexer->token_start;
    TokenType type = TOKEN_IDENTIFIER;

    switch (length) {
    case 2:
        if (strncmp(start, "if", 2) == 0)           type = TOKEN_IF;
        else if (strncmp(start, "in", 2) == 0)      type = TOKEN_IN;
        else if (strncmp(start, "or", 2) == 0)      type = TOKEN_OR;
        else if (strncmp(start, "as", 2) == 0)      type = TOKEN_AS;
        break;
    case 3:
        if (strncmp(start, "end", 3) == 0)          type = TOKEN_END;
        else if (strncmp(start, "var", 3) == 0)     type = TOKEN_VAR;
        else if (strncmp(start, "for", 3) == 0)     type = TOKEN_FOR;
        else if (strncmp(start, "and", 3) == 0)     type = TOKEN_AND;
        else if (strncmp(start, "not", 3) == 0)     type = TOKEN_NOT;
        break;
    case 4:
        if (strncmp(start, "func", 4) == 0)         type = TOKEN_FUNC;
        else if (strncmp(start, "else", 4) == 0)    type = TOKEN_ELSE;
        else if (strncmp(start, "step", 4) == 0)    type = TOKEN_STEP;
        else if (strncmp(start, "case", 4) == 0)    type = TOKEN_CASE;
        else if (strncmp(start, "enum", 4) == 0)    type = TOKEN_ENUM;
        else if (strncmp(start, "self", 4) == 0)    type = TOKEN_SELF;
        else if (strncmp(start, "null", 4) == 0)    type = TOKEN_NULL;
        else if (strncmp(start, "true", 4) == 0)    type = TOKEN_TRUE;
        else if (strncmp(start, "from", 4) == 0)    type = TOKEN_FROM;
        break;
    case 5:
        if (strncmp(start, "const", 5) == 0)        type = TOKEN_CONST;
        else if (strncmp(start, "until", 5) == 0)   type = TOKEN_UNTIL;
        else if (strncmp(start, "while", 5) == 0)   type = TOKEN_WHILE;
        else if (strncmp(start, "break", 5) == 0)   type = TOKEN_BREAK;
        else if (strncmp(start, "match", 5) == 0)   type = TOKEN_MATCH;
        else if (strncmp(start, "false", 5) == 0)   type = TOKEN_FALSE;
        break;
    case 6:
        if (strncmp(start, "return", 6) == 0)       type = TOKEN_RETURN;
        else if (strncmp(start, "export", 6) == 0)  type = TOKEN_EXPORT;
        else if (strncmp(start, "elseif", 6) == 0)  type = TOKEN_ELSEIF;
        else if (strncmp(start, "struct", 6) == 0)  type = TOKEN_STRUCT;
        else if (strncmp(start, "import", 6) == 0)  type = TOKEN_IMPORT;
        else if (strncmp(start, "extern", 6) == 0)  type = TOKEN_EXTERN;
        break;
    case 8:
        if (strncmp(start, "continue", 8) == 0)     type = TOKEN_CONTINUE;
        break;
    case 9:
        if (strncmp(start, "interface", 9) == 0)    type = TOKEN_INTERFACE;
        break;
    }

    tokens_push(lexer, type);
}

static void lexer_read_number(Lexer* lexer) {
    while (is_digit(lexer_peek(lexer))) {
        lexer_advance(lexer);
    }

    bool is_float = false;

    if (lexer_peek(lexer) == '.' && is_digit(lexer->current[1])) {
        is_float = true;
        lexer_advance(lexer);
        while (is_digit(lexer_peek(lexer))) {
            lexer_advance(lexer);
        }
    }

    if (lexer_peek(lexer) == 'f') {
        is_float = true;
        lexer_advance(lexer);
    }

    tokens_push(lexer, is_float ? TOKEN_FLOAT_LITERAL : TOKEN_INTEGER_LITERAL);
}

static void lexer_read_string(Lexer* lexer) {
    while (lexer_peek(lexer) != '"' && lexer_peek(lexer) != '\0') {
        if (lexer_peek(lexer) == '\n') {
            lexer->line++;
            lexer->column = 0;
        }
        lexer_advance(lexer);
    }

    if (lexer_peek(lexer) == '\0') {
        size_t start_offset = (size_t)(lexer->token_start - lexer->start);
        errors_push(lexer->errors, SEVERITY_ERROR, start_offset, lexer->token_line, lexer->token_column,
                     "Unterminated string literal.");
        tokens_push(lexer, TOKEN_ERROR);
        return;
    }

    lexer_advance(lexer);
    tokens_push(lexer, TOKEN_STRING_LITERAL);
}

void lexer_tokenize(Arena* arena, Tokens* tokens, Errors* errors, char* buffer, size_t buffer_size) {
    Lexer lexer;
    lexer.arena = arena;
    lexer.tokens = tokens;
    lexer.errors = errors;
    lexer.start = buffer;
    lexer.current = buffer;
    lexer.token_start = buffer;
    lexer.line = 1;
    lexer.column = 1;
    lexer.token_line = 1;
    lexer.token_column = 1;

    size_t estimated = buffer_size / 2;
    tokens->capacity = estimated < 256 ? 256 : estimated;
    tokens->count = 0;
    tokens->tokens = arena_alloc(arena, tokens->capacity * sizeof(Token));

    while (*lexer.current != '\0') {
        lexer.token_start = lexer.current;
        lexer.token_line = lexer.line;
        lexer.token_column = lexer.column;

        char c = lexer_advance(&lexer);

        switch (c) {
        case ' ':
        case '\t':
            break;

        case '\r':
            if (lexer_peek(&lexer) == '\n') {
                lexer_advance(&lexer);
            }
            tokens_push(&lexer, TOKEN_NEWLINE);
            lexer.line++;
            lexer.column = 1;
            break;

        case '\n':
            tokens_push(&lexer, TOKEN_NEWLINE);
            lexer.line++;
            lexer.column = 1;
            break;

        case '(': tokens_push(&lexer, TOKEN_LEFT_PAREN); break;
        case ')': tokens_push(&lexer, TOKEN_RIGHT_PAREN); break;
        case '[': tokens_push(&lexer, TOKEN_LEFT_BRACKET); break;
        case ']': tokens_push(&lexer, TOKEN_RIGHT_BRACKET); break;
        case ':': tokens_push(&lexer, TOKEN_COLON); break;
        case ',': tokens_push(&lexer, TOKEN_COMMA); break;
        case '.': tokens_push(&lexer, TOKEN_DOT); break;

        case '#':
            while (lexer_peek(&lexer) != '\n' && lexer_peek(&lexer) != '\0') {
                lexer_advance(&lexer);
            }
            break;

        case '+':
            if (lexer_match(&lexer, '=')) {
                tokens_push(&lexer, TOKEN_PLUS_ASSIGN);
            } else {
                tokens_push(&lexer, TOKEN_PLUS);
            }
            break;
        case '-':
            if (lexer_match(&lexer, '=')) {
                tokens_push(&lexer, TOKEN_MINUS_ASSIGN);
            } else {
                tokens_push(&lexer, TOKEN_MINUS);
            }
            break;
        case '*':
            if (lexer_match(&lexer, '=')) {
                tokens_push(&lexer, TOKEN_STAR_ASSIGN);
            } else {
                tokens_push(&lexer, TOKEN_STAR);
            }
            break;
        case '/':
            if (lexer_match(&lexer, '=')) {
                tokens_push(&lexer, TOKEN_SLASH_ASSIGN);
            } else {
                tokens_push(&lexer, TOKEN_SLASH);
            }
            break;

        case '&': tokens_push(&lexer, TOKEN_AMPERSAND); break;
        case '^': tokens_push(&lexer, TOKEN_CARET); break;

        case '=':
            if (lexer_match(&lexer, '=')) {
                tokens_push(&lexer, TOKEN_EQUAL);
            } else {
                tokens_push(&lexer, TOKEN_ASSIGN);
            }
            break;

        case '!':
            if (lexer_match(&lexer, '=')) {
                tokens_push(&lexer, TOKEN_NOT_EQUAL);
            } else {
                size_t offset = (size_t)(lexer.token_start - lexer.start);
                errors_push(errors, SEVERITY_ERROR, offset, lexer.token_line, lexer.token_column,
                             "Unexpected character '!'.");
                tokens_push(&lexer, TOKEN_ERROR);
            }
            break;

        case '<':
            if (lexer_match(&lexer, '=')) {
                tokens_push(&lexer, TOKEN_LESS_THAN_OR_EQUAL);
            } else {
                tokens_push(&lexer, TOKEN_LESS_THAN);
            }
            break;

        case '>':
            if (lexer_match(&lexer, '=')) {
                tokens_push(&lexer, TOKEN_GREATER_THAN_OR_EQUAL);
            } else {
                tokens_push(&lexer, TOKEN_GREATER_THAN);
            }
            break;

        case '"':
            lexer_read_string(&lexer);
            break;

        default:
            if (is_alpha(c)) {
                lexer_read_identifier_or_keyword(&lexer);
            } else if (is_digit(c)) {
                lexer_read_number(&lexer);
            } else {
                size_t offset = (size_t)(lexer.token_start - lexer.start);
                errors_push(errors, SEVERITY_ERROR, offset, lexer.token_line, lexer.token_column,
                             "Unexpected character '%c'.", c);
                tokens_push(&lexer, TOKEN_ERROR);
            }
            break;
        }
    }

    lexer.token_start = lexer.current;
    lexer.token_line = lexer.line;
    lexer.token_column = lexer.column;
    tokens_push(&lexer, TOKEN_END_OF_FILE);
}

void lexer_print(Tokens* tokens) {
    static char* token_names[] = {
        [TOKEN_INTEGER_LITERAL] = "INTEGER_LITERAL",
        [TOKEN_FLOAT_LITERAL] = "FLOAT_LITERAL",
        [TOKEN_STRING_LITERAL] = "STRING_LITERAL",
        [TOKEN_IDENTIFIER] = "IDENTIFIER",
        [TOKEN_FUNC] = "FUNC",
        [TOKEN_RETURN] = "RETURN",
        [TOKEN_END] = "END",
        [TOKEN_CONST] = "CONST",
        [TOKEN_EXPORT] = "EXPORT",
        [TOKEN_VAR] = "VAR",
        [TOKEN_IF] = "IF",
        [TOKEN_ELSEIF] = "ELSEIF",
        [TOKEN_ELSE] = "ELSE",
        [TOKEN_STRUCT] = "STRUCT",
        [TOKEN_INTERFACE] = "INTERFACE",
        [TOKEN_FOR] = "FOR",
        [TOKEN_IN] = "IN",
        [TOKEN_UNTIL] = "UNTIL",
        [TOKEN_STEP] = "STEP",
        [TOKEN_WHILE] = "WHILE",
        [TOKEN_BREAK] = "BREAK",
        [TOKEN_CONTINUE] = "CONTINUE",
        [TOKEN_MATCH] = "MATCH",
        [TOKEN_CASE] = "CASE",
        [TOKEN_ENUM] = "ENUM",
        [TOKEN_SELF] = "SELF",
        [TOKEN_NULL] = "NULL",
        [TOKEN_TRUE] = "TRUE",
        [TOKEN_FALSE] = "FALSE",
        [TOKEN_FROM] = "FROM",
        [TOKEN_IMPORT] = "IMPORT",
        [TOKEN_AND] = "AND",
        [TOKEN_OR] = "OR",
        [TOKEN_NOT] = "NOT",
        [TOKEN_AS] = "AS",
        [TOKEN_PLUS] = "PLUS",
        [TOKEN_MINUS] = "MINUS",
        [TOKEN_STAR] = "STAR",
        [TOKEN_SLASH] = "SLASH",
        [TOKEN_AMPERSAND] = "AMPERSAND",
        [TOKEN_CARET] = "CARET",
        [TOKEN_EQUAL] = "EQUAL",
        [TOKEN_NOT_EQUAL] = "NOT_EQUAL",
        [TOKEN_LESS_THAN] = "LESS_THAN",
        [TOKEN_GREATER_THAN] = "GREATER_THAN",
        [TOKEN_LESS_THAN_OR_EQUAL] = "LESS_THAN_OR_EQUAL",
        [TOKEN_GREATER_THAN_OR_EQUAL] = "GREATER_THAN_OR_EQUAL",
        [TOKEN_ASSIGN] = "ASSIGN",
        [TOKEN_PLUS_ASSIGN] = "PLUS_ASSIGN",
        [TOKEN_MINUS_ASSIGN] = "MINUS_ASSIGN",
        [TOKEN_STAR_ASSIGN] = "STAR_ASSIGN",
        [TOKEN_SLASH_ASSIGN] = "SLASH_ASSIGN",
        [TOKEN_LEFT_PAREN] = "LEFT_PAREN",
        [TOKEN_RIGHT_PAREN] = "RIGHT_PAREN",
        [TOKEN_LEFT_BRACKET] = "LEFT_BRACKET",
        [TOKEN_RIGHT_BRACKET] = "RIGHT_BRACKET",
        [TOKEN_COLON] = "COLON",
        [TOKEN_COMMA] = "COMMA",
        [TOKEN_DOT] = "DOT",
        [TOKEN_NEWLINE] = "NEWLINE",
        [TOKEN_END_OF_FILE] = "END_OF_FILE",
        [TOKEN_ERROR] = "ERROR",
    };

    for (size_t i = 0; i < tokens->count; i++) {
        Token* tok = &tokens->tokens[i];
        if (tok->type == TOKEN_NEWLINE) {
            printf("%s %zu:%zu\n", token_names[tok->type], tok->line, tok->column);
        } else {
            printf("%s %zu:%zu %.*s\n", token_names[tok->type], tok->line, tok->column, (int)tok->size, tok->value);
        }
    }
}
