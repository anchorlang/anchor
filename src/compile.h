#ifndef ANCC_COMPILE_H
#define ANCC_COMPILE_H

#include "arena.h"
#include "error.h"
#include "package.h"
#include "module.h"

#include <stdbool.h>

bool compile(Arena* arena, Errors* errors, Package* pkg, ModuleGraph* graph, char* output_dir);

#endif
