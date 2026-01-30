#include "package.h"
#include "fs.h"

#include <stdio.h>
#include <string.h>

static char* arena_strdup(Arena* arena, char* src, size_t len) {
    char* dst = arena_alloc(arena, len + 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

bool package_load(Arena* arena, Errors* errors, Package* pkg, char* dir) {
    memset(pkg, 0, sizeof(Package));

    char path[1024];
    snprintf(path, sizeof(path), "%s/anchor", dir);

    size_t size;
    char* buf = file_read(arena, path, &size);
    if (!buf) {
        errors_push(errors, SEVERITY_ERROR, 0, 0, 0, "cannot open '%s'", path);
        return false;
    }

    size_t line = 1;
    size_t pos = 0;

    while (pos < size) {
        // skip blank lines
        if (buf[pos] == '\n') {
            pos++;
            line++;
            continue;
        }
        if (buf[pos] == '\r') {
            pos++;
            if (pos < size && buf[pos] == '\n') pos++;
            line++;
            continue;
        }

        // read key
        size_t key_start = pos;
        while (pos < size && buf[pos] != ' ' && buf[pos] != '\t' && buf[pos] != '\n' && buf[pos] != '\r') {
            pos++;
        }
        size_t key_len = pos - key_start;

        // skip whitespace between key and value
        while (pos < size && (buf[pos] == ' ' || buf[pos] == '\t')) {
            pos++;
        }

        // read value
        size_t val_start = pos;
        while (pos < size && buf[pos] != '\n' && buf[pos] != '\r') {
            pos++;
        }

        // trim trailing whitespace from value
        size_t val_end = pos;
        while (val_end > val_start && (buf[val_end - 1] == ' ' || buf[val_end - 1] == '\t')) {
            val_end--;
        }
        size_t val_len = val_end - val_start;

        // match key
        if (key_len == 4 && memcmp(buf + key_start, "name", 4) == 0) {
            pkg->name = arena_strdup(arena, buf + val_start, val_len);
        } else if (key_len == 5 && memcmp(buf + key_start, "entry", 5) == 0) {
            pkg->entry = arena_strdup(arena, buf + val_start, val_len);
        } else {
            char key_buf[64];
            size_t copy_len = key_len < 63 ? key_len : 63;
            memcpy(key_buf, buf + key_start, copy_len);
            key_buf[copy_len] = '\0';
            errors_push(errors, SEVERITY_ERROR, 0, line, 1, "unknown key '%s'", key_buf);
        }

        // advance past newline
        if (pos < size && buf[pos] == '\r') pos++;
        if (pos < size && buf[pos] == '\n') pos++;
        line++;
    }

    bool ok = true;
    if (!pkg->name) {
        errors_push(errors, SEVERITY_ERROR, 0, 0, 0, "missing required field 'name' in '%s'", path);
        ok = false;
    }
    if (!pkg->entry) {
        errors_push(errors, SEVERITY_ERROR, 0, 0, 0, "missing required field 'entry' in '%s'", path);
        ok = false;
    }
    return ok;
}
