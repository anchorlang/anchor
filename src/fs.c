#include "fs.h"
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

bool dir_ensure(char* path) {
    if (dir_exists(path)) return true;
    return CreateDirectoryA(path, NULL) != 0;
}
#else
#include <sys/stat.h>
#include <dirent.h>

bool dir_exists(char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool file_exists(char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool dir_ensure(char* path) {
    if (dir_exists(path)) return true;
    return mkdir(path, 0755) == 0;
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

bool has_extension(char* path, char* extension) {
    size_t path_len = strlen(path);
    size_t ext_len = strlen(extension);
    if (path_len < ext_len) return false;
    return strcmp(path + path_len - ext_len, extension) == 0;
}

#ifdef _WIN32

typedef struct {
    HANDLE find_handle;
    WIN32_FIND_DATAA find_data;
    bool first;
} WinDirCtx;

bool dir_iter_open(DirIter* iter, char* dir) {
    memset(iter, 0, sizeof(DirIter));
    snprintf(iter->dir, DIR_ITER_MAX_PATH, "%s", dir);

    char pattern[DIR_ITER_MAX_PATH];
    snprintf(pattern, DIR_ITER_MAX_PATH, "%s\\*", dir);

    WinDirCtx* ctx = malloc(sizeof(WinDirCtx));
    ctx->find_handle = FindFirstFileA(pattern, &ctx->find_data);
    if (ctx->find_handle == INVALID_HANDLE_VALUE) {
        free(ctx);
        return false;
    }
    ctx->first = true;
    iter->handle = ctx;
    return true;
}

bool dir_iter_next(DirIter* iter) {
    WinDirCtx* ctx = (WinDirCtx*)iter->handle;
    if (!ctx) return false;

    for (;;) {
        if (ctx->first) {
            ctx->first = false;
        } else {
            if (!FindNextFileA(ctx->find_handle, &ctx->find_data)) {
                return false;
            }
        }

        if (strcmp(ctx->find_data.cFileName, ".") == 0) continue;
        if (strcmp(ctx->find_data.cFileName, "..") == 0) continue;

        snprintf(iter->entry.name, sizeof(iter->entry.name), "%s", ctx->find_data.cFileName);
        snprintf(iter->entry.path, DIR_ITER_MAX_PATH, "%s/%s", iter->dir, ctx->find_data.cFileName);
        iter->entry.is_dir = (ctx->find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return true;
    }
}

void dir_iter_close(DirIter* iter) {
    WinDirCtx* ctx = (WinDirCtx*)iter->handle;
    if (ctx) {
        FindClose(ctx->find_handle);
        free(ctx);
        iter->handle = NULL;
    }
}

#else

bool dir_iter_open(DirIter* iter, char* dir) {
    memset(iter, 0, sizeof(DirIter));
    snprintf(iter->dir, DIR_ITER_MAX_PATH, "%s", dir);

    DIR* dp = opendir(dir);
    if (!dp) return false;
    iter->handle = dp;
    return true;
}

bool dir_iter_next(DirIter* iter) {
    DIR* dp = (DIR*)iter->handle;
    if (!dp) return false;

    for (;;) {
        struct dirent* entry = readdir(dp);
        if (!entry) return false;

        if (strcmp(entry->d_name, ".") == 0) continue;
        if (strcmp(entry->d_name, "..") == 0) continue;

        snprintf(iter->entry.name, sizeof(iter->entry.name), "%s", entry->d_name);
        snprintf(iter->entry.path, DIR_ITER_MAX_PATH, "%s/%s", iter->dir, entry->d_name);
        iter->entry.is_dir = dir_exists(iter->entry.path);
        return true;
    }
}

void dir_iter_close(DirIter* iter) {
    DIR* dp = (DIR*)iter->handle;
    if (dp) {
        closedir(dp);
        iter->handle = NULL;
    }
}

#endif
