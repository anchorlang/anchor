#include "arena.h"
#include "lexer.h"
#include "parser.h"
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

        size_t buffer_size;
        char* buffer = file_read(&arena, argv[2], &buffer_size);
        if (!buffer) {
            fprintf(stderr, "Error: File not found '%s'.\n", argv[2]);
            arena_free(&arena);
            return EXIT_FAILURE;
        }

        Errors errors;
        errors_init(&arena, &errors);

        Tokens tokens;
        lexer_tokenize(&arena, &tokens, &errors, buffer, buffer_size);

        lexer_print(&tokens);

        for (Error* error = errors.first; error; error = error->next) {
            fprintf(stderr, "%zu:%zu: %s\n", error->line, error->column, error->message);
        }

        arena_free(&arena);
        return errors.count > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "ast") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: ancc ast [file]\n");
            return EXIT_FAILURE;
        }

        Arena arena;
        arena_init(&arena, 16 * 1024 * 1024);

        size_t buffer_size;
        char* buffer = file_read(&arena, argv[2], &buffer_size);
        if (!buffer) {
            fprintf(stderr, "Error: File not found '%s'.\n", argv[2]);
            arena_free(&arena);
            return EXIT_FAILURE;
        }

        Errors errors;
        errors_init(&arena, &errors);

        Tokens tokens;
        lexer_tokenize(&arena, &tokens, &errors, buffer, buffer_size);

        Node* ast = parser_parse(&arena, &tokens, &errors);
        ast_print(ast, 0);

        for (Error* error = errors.first; error; error = error->next) {
            fprintf(stderr, "%zu:%zu: %s\n", error->line, error->column, error->message);
        }

        arena_free(&arena);
        return errors.count > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    fprintf(stderr, "Error: Unknown command '%s'.\n", argv[1]);
    return EXIT_FAILURE;
}
