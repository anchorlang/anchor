#include "compile.h"
#include "os.h"

#include <stdio.h>
#include <string.h>

bool compile(Arena* arena, Errors* errors, Package* pkg, ModuleGraph* graph, char* output_dir) {
    size_t dir_len = strlen(output_dir);
    size_t name_len = strlen(pkg->name);

    // estimate command length
    // "gcc -std=c99 -o {dir}/{name}[.exe]" + per-module " {dir}/anc__{name}__{mod}.c" + " 2>&1\0"
    size_t cmd_cap = 64 + dir_len + name_len;
    for (Module* m = graph->first; m; m = m->next) {
        if (!m->symbols) continue;
        cmd_cap += dir_len + name_len + strlen(m->name) + 32;
    }

    char* cmd = arena_alloc(arena, cmd_cap);
    int pos = snprintf(cmd, cmd_cap, "gcc -std=c99 -o %s/%s", output_dir, pkg->name);
#ifdef _WIN32
    pos += snprintf(cmd + pos, cmd_cap - pos, ".exe");
#endif
    for (Module* m = graph->first; m; m = m->next) {
        if (!m->symbols) continue;
        pos += snprintf(cmd + pos, cmd_cap - pos,
                        " %s/anc__%s__%s.c", output_dir, pkg->name, m->name);
    }
    pos += snprintf(cmd + pos, cmd_cap - pos, " 2>&1");

    char cc_output[4096];
    int status = os_cmd_run(cmd, cc_output, sizeof(cc_output));
    if (status != 0) {
        errors_push(errors, SEVERITY_ERROR, 0, 0, 0, "C compilation failed");
        if (cc_output[0] != '\0') {
            fprintf(stderr, "%s", cc_output);
        }
        return false;
    }

    return true;
}
