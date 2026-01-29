#ifndef ANCC_ERROR_H
#define ANCC_ERROR_H

#include "arena.h"

#include <stddef.h>

typedef enum Severity {
    SEVERITY_ERROR,
    SEVERITY_WARNING,
    SEVERITY_HINT,
} Severity;

typedef struct Error {
    struct Error* next;
    Severity severity;
    size_t offset;
    size_t line;
    size_t column;
    char message[];
} Error;

typedef struct Errors {
    Arena* arena;
    Error* first;
    Error* last;
    size_t count;
} Errors;

void errors_init(Arena* arena, Errors* errors);

void errors_push(Errors* errors, Severity severity, size_t offset, size_t line, size_t column, char* message, ...);

#endif
