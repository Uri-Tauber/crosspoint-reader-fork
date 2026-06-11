#include "ParseArena.h"

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Global "current arena" for the parsing task.
// ESP32-C3 is single-core and our parsing runs on one task, so thread-local is not strictly required.
static arena_t* g_active_parse_arena = NULL;

void parse_arena_activate(arena_t* arena) { g_active_parse_arena = arena; }

void parse_arena_deactivate(void) { g_active_parse_arena = NULL; }

arena_t* parse_arena_get(void) { return g_active_parse_arena; }

// Memory structure when using parse_realloc to track sizes for proper reallocation handling
// [ size_t size ] [ payload ... ]

void* parse_malloc(size_t size) {
  if (g_active_parse_arena) {
    // Allocate space for size tracking prefix
    size_t* ptr = (size_t*)arena_alloc(g_active_parse_arena, size + sizeof(size_t));
    if (ptr) {
      *ptr = size;
      return ptr + 1;
    }
    return NULL;
  }

  size_t* ptr = (size_t*)malloc(size + sizeof(size_t));
  if (ptr) {
    *ptr = size;
    return ptr + 1;
  }
  return NULL;
}

void* parse_realloc(void* ptr, size_t size) {
  if (!ptr) {
    return parse_malloc(size);
  }

  if (size == 0) {
    parse_free(ptr);
    return NULL;
  }

  size_t* real_ptr = (size_t*)ptr - 1;
  size_t old_size = *real_ptr;

  if (g_active_parse_arena) {
    size_t* new_ptr =
        (size_t*)arena_realloc(g_active_parse_arena, real_ptr, old_size + sizeof(size_t), size + sizeof(size_t));
    if (new_ptr) {
      *new_ptr = size;
      return new_ptr + 1;
    }
    return NULL;
  }

  size_t* new_ptr = (size_t*)realloc(real_ptr, size + sizeof(size_t));
  if (new_ptr) {
    *new_ptr = size;
    return new_ptr + 1;
  }
  return NULL;
}

void parse_free(void* ptr) {
  if (!ptr) return;

  if (g_active_parse_arena) {
    // No-op for arena, all memory is freed in bulk.
    return;
  }

  size_t* real_ptr = (size_t*)ptr - 1;
  free(real_ptr);
}

#ifdef __cplusplus
}
#endif
