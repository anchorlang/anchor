#ifndef ANCC_PARSER_H
#define ANCC_PARSER_H

#include "arena.h"
#include "ast.h"
#include "error.h"
#include "lexer.h"

Node* parser_parse(Arena* arena, Tokens* tokens, Errors* errors);

void ast_print(Node* node, int indent);

#endif
