#include "arena.h"
#include "macro.h"

#include <stdlib.h>

static ArenaBlock* block_create(size_t size) {
    ArenaBlock* block = malloc(sizeof(ArenaBlock) + size);
    block->next = NULL;
    block->offset = 0;
    block->size = size;
    return block;
}

void arena_init(Arena* arena, size_t block_size) {
    arena->first = block_create(block_size);
    arena->last = arena->first;
    arena->block_size = block_size;
}

void arena_free(Arena* arena) {
    ArenaBlock* block = arena->first, *next;
    while (block) {
        next = block->next;
        free(block);
        block = next;
    }
}

void arena_reset(Arena* arena) {
    ArenaBlock* block = arena->first->next, *next;
    while (block) {
        next = block->next;
        free(block);
        block = next;
    }

    arena->first->next = NULL;
    arena->first->offset = 0;
    arena->last = arena->first;
}

void* arena_alloc(Arena* arena, size_t size) {
    size_t aligned_size = ALIGN_UP(size, sizeof(void*));

    ArenaBlock* block = arena->last;
    if (block->offset + aligned_size > block->size) {
        size_t block_size = aligned_size > arena->block_size ? aligned_size : arena->block_size;
        block->next = block_create(block_size);
        arena->last = block->next;
        block = arena->last;
    }

    void* data = &block->data[block->offset];
    block->offset += aligned_size;
    return data;
}
