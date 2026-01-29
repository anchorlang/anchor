#include "lexer.h"

typedef struct Lexer {
    Arena* arena;
    size_t line;
    size_t column;
} Lexer;

void lexer_tokenize(Arena* arena, Tokens* tokens, Errors* errors, char* buffer) {
    Lexer lexer;
    lexer.arena = arena;
    lexer.line = 1;
    lexer.column = 1;
}
