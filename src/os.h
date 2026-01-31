#ifndef ANCC_OS_H
#define ANCC_OS_H

#include <stddef.h>

// Run a command, capture stdout+stderr into output buffer. Returns exit status.
int os_cmd_run(const char* cmd, char* output, size_t output_cap);

#endif
