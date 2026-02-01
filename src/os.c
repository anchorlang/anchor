#include "os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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

bool os_cwd(char* buf, size_t buf_cap) {
#ifdef _WIN32
    DWORD len = GetCurrentDirectoryA((DWORD)buf_cap, buf);
    if (len == 0 || len >= buf_cap) return false;
    return true;
#else
    return getcwd(buf, buf_cap) != NULL;
#endif
}

bool os_tmp_dir(char* buf, size_t buf_cap) {
#ifdef _WIN32
    DWORD len = GetTempPathA((DWORD)buf_cap, buf);
    if (len == 0 || len >= buf_cap) return false;
    if (len > 0 && buf[len - 1] == '\\') buf[len - 1] = '\0';
    return true;
#else
    const char* tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    size_t len = strlen(tmp);
    if (len >= buf_cap) return false;
    memcpy(buf, tmp, len + 1);
    return true;
#endif
}
