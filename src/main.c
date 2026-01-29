#include "arena.h"
#include "lexer.h"
#include "fs.h"
#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage: ancc <command> [options]\n"
            "Commands:\n"
            "  ancc build [dir]     Build package.\n"
            "  ancc compile [file]  Compile one file.\n"
            "  ancc lsp [dir]       Run LSP mode.\n"
            "  ancc lexer [file]    Print tokens.\n"
            "  ancc ast [file]      Print ast.\n"
        );
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "lexer") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: ancc lexer [file]\n");
            return EXIT_FAILURE;
        }

        Arena arena;
        arena_init(&arena, 16 * 1024 * 1024);

        char* buffer = file_read(&arena, argv[2]);
        if (!buffer) {
            fprintf(stderr, "Error: File not found '%s'.\n", argv[2]);
            arena_free(&arena);
            return EXIT_FAILURE;
        }

        Errors errors;
        errors_init(&arena, &errors);

        Tokens tokens;
        lexer_tokenize(&arena, &tokens, &errors, buffer);

        arena_free(&arena);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Error: Unknown command '%s'.\n", argv[1]);
    return EXIT_FAILURE;
}
