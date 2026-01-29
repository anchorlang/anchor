#include "error.h"
#include "arena.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

void errors_init(Arena* arena, Errors* errors) {
    errors->arena = arena;
    errors->first = NULL;
    errors->last = NULL;
    errors->count = 0;
}

void errors_push(Errors* errors, Severity severity, size_t offset, size_t line, size_t column, char* message, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, message);
    int written = vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);

    size_t message_length = written < 0 ? 0 : (written >= sizeof(buffer) ? sizeof(buffer) - 1 : (size_t)written);

    Error* error = arena_alloc(errors->arena, sizeof(Error) + message_length + 1);
    error->next = NULL;
    error->severity = severity;
    error->offset = offset;
    error->line = line;
    error->column = column;
    memcpy(error->message, buffer, message_length);
    error->message[message_length] = '\0';

    if (!errors->first) {
        errors->first = error;
    } else {
        errors->last->next = error;
    }

    errors->last = error;
}
