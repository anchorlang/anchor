#include "fs.h"
#include "arena.h"

#include <stdio.h>

#ifdef _WIN32
#include <windows.h>

bool dir_exists(char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool file_exists(char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
#else
#include <sys/stat.h>

bool dir_exists(char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool file_exists(char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
#endif

char* file_read(Arena* arena, char* path, size_t* out_size) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size_t size = (size_t)ftell(file);
    fseek(file, 0, SEEK_SET);

    char* data = arena_alloc(arena, size + 1);
    fread(data, 1, size, file);
    data[size] = '\0';

    fclose(file);
    if (out_size) {
        *out_size = size;
    }
    return data;
}
