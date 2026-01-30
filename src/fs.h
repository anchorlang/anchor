#ifndef ANCC_FS_H
#define ANCC_FS_H

#include "arena.h"

#include <stdbool.h>

bool dir_exists(char* path);

bool file_exists(char* path);

char* file_read(Arena* arena, char* path, size_t* out_size);

#endif
