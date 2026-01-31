#include "os.h"

#include <stdio.h>

int os_cmd_run(const char* cmd, char* output, size_t output_cap) {
#ifdef _WIN32
    FILE* proc = _popen(cmd, "r");
#else
    FILE* proc = popen(cmd, "r");
#endif
    if (!proc) {
        if (output_cap > 0) output[0] = '\0';
        return -1;
    }

    size_t len = 0;
    size_t n;
    while ((n = fread(output + len, 1, output_cap - len - 1, proc)) > 0) {
        len += n;
        if (len + 1 >= output_cap) break;
    }
    output[len] = '\0';

#ifdef _WIN32
    return _pclose(proc);
#else
    return pclose(proc);
#endif
}
