#ifndef ANCC_LSP_ANALYSIS_H
#define ANCC_LSP_ANALYSIS_H

#include "arena.h"
#include "error.h"
#include "module.h"
#include "lsp_json.h"

typedef struct LspAnalysisResult {
    ModuleGraph graph;
    Errors errors;
    Module* main_module;
} LspAnalysisResult;

/* Run lexer + parser + sema on a single file, using override_source as the
   file content instead of reading from disk. src_dir is the directory
   containing the file. module_name/module_name_len is the stem (e.g. "main"). */
LspAnalysisResult lsp_analyze(Arena* arena, char* src_dir,
                              char* override_path, char* override_source,
                              size_t override_source_len,
                              char* module_name, size_t module_name_len);

/* Write a JSON array of LSP Diagnostic objects from the error list. */
void lsp_errors_to_diagnostics(JsonWriter* jw, Errors* errors);

#endif
