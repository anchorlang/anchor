#include "lsp_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

void lsp_transport_init(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

/* Read one LSP message from stdin.
   Format: "Content-Length: N\r\n\r\n<body of N bytes>"
   Returns heap-allocated buffer (caller must free). */
char* lsp_transport_read(size_t* out_len) {
    // Read headers line by line
    size_t content_length = 0;
    char header[256];

    for (;;) {
        if (!fgets(header, sizeof(header), stdin)) return NULL;
        // Blank line (just \r\n) marks end of headers
        if (strcmp(header, "\r\n") == 0 || strcmp(header, "\n") == 0) break;

        if (strncmp(header, "Content-Length:", 15) == 0) {
            content_length = (size_t)atol(header + 15);
        }
    }

    if (content_length == 0) return NULL;

    char* body = malloc(content_length + 1);
    if (!body) return NULL;

    size_t read = fread(body, 1, content_length, stdin);
    if (read != content_length) {
        free(body);
        return NULL;
    }

    body[content_length] = '\0';
    if (out_len) *out_len = content_length;
    return body;
}

void lsp_transport_write(const char* body, size_t len) {
    fprintf(stdout, "Content-Length: %zu\r\n\r\n", len);
    fwrite(body, 1, len, stdout);
    fflush(stdout);
}
