#include "arena.h"
#include "lexer.h"
#include "parser.h"
#include "module.h"
#include "sema.h"
#include "package.h"
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

    if (strcmp(argv[1], "build") == 0) {
        char* dir = argc >= 3 ? argv[2] : ".";

        Arena arena;
        arena_init(&arena, 16 * 1024 * 1024);

        Errors errors;
        errors_init(&arena, &errors);

        Package pkg;
        if (!package_load(&arena, &errors, &pkg, dir)) {
            for (Error* error = errors.first; error; error = error->next) {
                fprintf(stderr, "error: %s\n", error->message);
            }
            arena_free(&arena);
            return EXIT_FAILURE;
        }

        char src_dir[1024];
        snprintf(src_dir, sizeof(src_dir), "%s/src", dir);

        ModuleGraph graph;
        module_graph_init(&graph, &arena, &errors, src_dir);

        size_t entry_len = strlen(pkg.entry);
        Module* entry = module_resolve(&graph, pkg.entry, entry_len);
        if (!entry) {
            for (Error* error = errors.first; error; error = error->next) {
                fprintf(stderr, "error: %s\n", error->message);
            }
            arena_free(&arena);
            return EXIT_FAILURE;
        }

        sema_analyze(&arena, &errors, &graph);

        printf("package: %s\n", pkg.name);
        printf("modules: %d\n", graph.count);
        for (Module* m = graph.first; m; m = m->next) {
            printf("  %s (%s)\n", m->name, m->path);
            if (m->symbols) {
                static const char* kind_names[] = {
                    "FUNC", "STRUCT", "INTERFACE", "CONST", "VAR", "IMPORT"
                };
                for (Symbol* s = m->symbols->first; s; s = s->next) {
                    printf("    %s %.*s", kind_names[s->kind], (int)s->name_size, s->name);
                    if (s->node && s->node->resolved_type && s->kind == SYMBOL_FUNC) {
                        printf(" %s", type_name((Type*)s->node->resolved_type));
                    }
                    if (s->is_export) printf(" [export]");
                    if (s->source) printf(" <- %s", s->source->name);
                    printf("\n");
                }
            }
        }

        for (Error* error = errors.first; error; error = error->next) {
            fprintf(stderr, "%zu:%zu: %s\n", error->line, error->column, error->message);
        }

        arena_free(&arena);
        return errors.count > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    fprintf(stderr, "Error: Unknown command '%s'.\n", argv[1]);
    return EXIT_FAILURE;
}
