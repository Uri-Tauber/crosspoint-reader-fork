#pragma once

#include <stddef.h>

#include "arena.h"

#ifdef __cplusplus
extern "C" {
#endif

// Activate an arena for the current parsing session
void parse_arena_activate(arena_t* arena);

// Deactivate the current arena
void parse_arena_deactivate(void);

// Get the currently active arena
arena_t* parse_arena_get(void);

// Wrapper for malloc
void* parse_malloc(size_t size);

// Wrapper for realloc
void* parse_realloc(void* ptr, size_t size);

// Wrapper for free
void parse_free(void* ptr);

#ifdef __cplusplus
}
#endif
