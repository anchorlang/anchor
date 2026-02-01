#include "lsp_analysis.h"
#include "sema.h"

LspAnalysisResult lsp_analyze(Arena* arena, char* src_dir,
                              char* override_path, char* override_source,
                              size_t override_source_len,
                              char* module_name, size_t module_name_len) {
    LspAnalysisResult result;
    errors_init(arena, &result.errors);

    module_graph_init(&result.graph, arena, &result.errors, src_dir);
    result.graph.override_path = override_path;
    result.graph.override_source = override_source;
    result.graph.override_source_len = override_source_len;

    result.main_module = module_resolve(&result.graph, module_name, module_name_len);
    if (result.main_module) {
        sema_analyze(arena, &result.errors, &result.graph);
    }

    return result;
}

void lsp_errors_to_diagnostics(JsonWriter* jw, Errors* errors) {
    jw_array_start(jw);
    for (Error* e = errors->first; e; e = e->next) {
        jw_object_start(jw);

        // range (0-based line/col; point range since we lack end positions)
        jw_key(jw, "range");
        jw_object_start(jw);
            jw_key(jw, "start");
            jw_object_start(jw);
                jw_key(jw, "line");
                jw_int(jw, e->line > 0 ? (int)(e->line - 1) : 0);
                jw_key(jw, "character");
                jw_int(jw, e->column > 0 ? (int)(e->column - 1) : 0);
            jw_object_end(jw);
            jw_key(jw, "end");
            jw_object_start(jw);
                jw_key(jw, "line");
                jw_int(jw, e->line > 0 ? (int)(e->line - 1) : 0);
                jw_key(jw, "character");
                jw_int(jw, e->column > 0 ? (int)(e->column - 1) : 0);
            jw_object_end(jw);
        jw_object_end(jw);

        // severity: 1=Error, 2=Warning, 4=Hint
        jw_key(jw, "severity");
        switch (e->severity) {
        case SEVERITY_ERROR:   jw_int(jw, 1); break;
        case SEVERITY_WARNING: jw_int(jw, 2); break;
        case SEVERITY_HINT:    jw_int(jw, 4); break;
        }

        jw_key(jw, "source");
        jw_string(jw, "anchor");

        jw_key(jw, "message");
        jw_string(jw, e->message);

        jw_object_end(jw);
    }
    jw_array_end(jw);
}
