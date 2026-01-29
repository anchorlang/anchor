#ifndef ANCC_ARENA_H
#define ANCC_ARENA_H

#include <stddef.h>
#include <stdint.h>

typedef struct ArenaBlock {
    struct ArenaBlock* next;
    size_t offset;
    size_t size;
    uint8_t data[];
} ArenaBlock;

typedef struct Arena {
    ArenaBlock* first;
    ArenaBlock* last;
    size_t block_size;
} Arena;

void arena_init(Arena* arena, size_t block_size);

void arena_free(Arena* arena);

void arena_reset(Arena* arena);

void* arena_alloc(Arena* arena, size_t size);

#endif
