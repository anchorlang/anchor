#ifndef ANCC_FS_H
#define ANCC_FS_H

#include "arena.h"

#include <stdbool.h>

#define DIR_ITER_MAX_PATH 1024

typedef struct DirEntry {
    char name[256];
    char path[DIR_ITER_MAX_PATH];
    bool is_dir;
} DirEntry;

typedef struct DirIter {
    DirEntry entry;
    char dir[DIR_ITER_MAX_PATH];
    void* handle;
} DirIter;

bool dir_exists(char* path);

bool file_exists(char* path);

char* file_read(Arena* arena, char* path, size_t* out_size);

bool has_extension(char* path, char* extension);

bool dir_ensure(char* path);

bool dir_iter_open(DirIter* iter, char* dir);

bool dir_iter_next(DirIter* iter);

void dir_iter_close(DirIter* iter);

#endif
