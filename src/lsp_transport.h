#ifndef ANCC_LSP_TRANSPORT_H
#define ANCC_LSP_TRANSPORT_H

#include <stddef.h>

void lsp_transport_init(void);
char* lsp_transport_read(size_t* out_len);
void lsp_transport_write(const char* body, size_t len);

#endif
