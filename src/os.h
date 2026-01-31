#ifndef ANCC_OS_H
#define ANCC_OS_H

#include <stdbool.h>
#include <stddef.h>

// Run a command, capture stdout+stderr into output buffer. Returns exit status.
int os_cmd_run(const char* cmd, char* output, size_t output_cap);

// Write the system temp directory path into buf. Returns false on failure.
bool os_tmp_dir(char* buf, size_t buf_cap);

#endif
