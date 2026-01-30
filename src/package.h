#ifndef ANCC_PACKAGE_H
#define ANCC_PACKAGE_H

#include "arena.h"
#include "error.h"

#include <stdbool.h>

typedef struct Package {
    char* name;
    char* entry;
} Package;

bool package_load(Arena* arena, Errors* errors, Package* pkg, char* dir);

#endif
