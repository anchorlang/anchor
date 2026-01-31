#include "arena.h"
#include "lexer.h"
#include "parser.h"
#include "module.h"
#include "sema.h"
#include "codegen.h"
#include "package.h"
#include "fs.h"
#include "compile.h"
#include "os.h"
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
            "  ancc run <file>      Compile and run a file.\n"
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

        char output_dir[1024];
        snprintf(output_dir, sizeof(output_dir), "%s/build", dir);

        if (errors.count == 0) {
            codegen(&arena, &errors, &pkg, &graph, entry, output_dir);
        }

        if (errors.count == 0) {
            compile(&arena, &errors, &pkg, &graph, output_dir);
        }

        for (Error* error = errors.first; error; error = error->next) {
            fprintf(stderr, "%zu:%zu: %s\n", error->line, error->column, error->message);
        }

        arena_free(&arena);
        return errors.count > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: ancc run <file>\n");
            return EXIT_FAILURE;
        }

        char* file_path = argv[2];

        // Extract directory and stem from file path
        char src_dir[1024];
        char stem[256];

        // Find last slash (handle both / and \)
        char* last_slash = NULL;
        for (char* p = file_path; *p; p++) {
            if (*p == '/' || *p == '\\') last_slash = p;
        }

        if (last_slash) {
            size_t dir_len = (size_t)(last_slash - file_path);
            if (dir_len >= sizeof(src_dir)) dir_len = sizeof(src_dir) - 1;
            memcpy(src_dir, file_path, dir_len);
            src_dir[dir_len] = '\0';
        } else {
            src_dir[0] = '.';
            src_dir[1] = '\0';
        }

        // Extract stem: filename without extension
        char* filename = last_slash ? last_slash + 1 : file_path;
        char* dot = strrchr(filename, '.');
        size_t stem_len = dot ? (size_t)(dot - filename) : strlen(filename);
        if (stem_len >= sizeof(stem)) stem_len = sizeof(stem) - 1;
        memcpy(stem, filename, stem_len);
        stem[stem_len] = '\0';

        Arena arena;
        arena_init(&arena, 16 * 1024 * 1024);

        Errors errors;
        errors_init(&arena, &errors);

        // Synthetic package (no anchor manifest needed)
        Package pkg;
        pkg.name = stem;
        pkg.entry = stem;

        ModuleGraph graph;
        module_graph_init(&graph, &arena, &errors, src_dir);

        Module* entry = module_resolve(&graph, stem, stem_len);
        if (!entry) {
            for (Error* error = errors.first; error; error = error->next) {
                fprintf(stderr, "error: %s\n", error->message);
            }
            arena_free(&arena);
            return EXIT_FAILURE;
        }

        sema_analyze(&arena, &errors, &graph);

        // Temp build directory: {tmp}/ancc/{stem}/
        char tmp[1024];
        if (!os_tmp_dir(tmp, sizeof(tmp))) {
            fprintf(stderr, "error: cannot determine temp directory\n");
            arena_free(&arena);
            return EXIT_FAILURE;
        }

        char ancc_tmp[1024];
        snprintf(ancc_tmp, sizeof(ancc_tmp), "%s/ancc", tmp);
        dir_ensure(ancc_tmp);

        char output_dir[1024];
        snprintf(output_dir, sizeof(output_dir), "%s/ancc/%s", tmp, stem);
        dir_ensure(output_dir);

        if (errors.count == 0) {
            codegen(&arena, &errors, &pkg, &graph, entry, output_dir);
        }

        if (errors.count == 0) {
            compile(&arena, &errors, &pkg, &graph, output_dir);
        }

        if (errors.count > 0) {
            for (Error* error = errors.first; error; error = error->next) {
                fprintf(stderr, "%zu:%zu: %s\n", error->line, error->column, error->message);
            }
            arena_free(&arena);
            return EXIT_FAILURE;
        }

        // Execute the binary
        char bin_path[1024];
#ifdef _WIN32
        snprintf(bin_path, sizeof(bin_path), "%s/%s.exe", output_dir, stem);
#else
        snprintf(bin_path, sizeof(bin_path), "%s/%s", output_dir, stem);
#endif

        char run_output[4096];
        int status = os_cmd_run(bin_path, run_output, sizeof(run_output));
        if (run_output[0] != '\0') printf("%s", run_output);

        arena_free(&arena);
        return status;
    }

    fprintf(stderr, "Error: Unknown command '%s'.\n", argv[1]);
    return EXIT_FAILURE;
}
