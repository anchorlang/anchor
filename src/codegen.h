#ifndef ANCC_CODEGEN_H
#define ANCC_CODEGEN_H

#include "arena.h"
#include "error.h"
#include "package.h"
#include "module.h"

#include <stdbool.h>

bool codegen(Arena* arena, Errors* errors, Package* pkg, ModuleGraph* graph, char* output_dir);

#endif
