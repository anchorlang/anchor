#ifndef ANCC_LSP_SERVER_H
#define ANCC_LSP_SERVER_H

/* Start the LSP server. Blocks until exit.
   dir is the workspace root directory (or "." if not specified). */
void lsp_server_run(char* dir);

#endif
