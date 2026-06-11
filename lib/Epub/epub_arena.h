#pragma once
#include <arena_allocator.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Single arena used during EPUB XML parsing to eliminate heap fragmentation.
// Lifecycle: activate before parsing a section, deactivate after serialization completes.
// All Expat internal allocations are redirected here during that window.

// Activate an arena for the current parsing scope.
void epub_arena_activate(arena_t* arena);

// Deactivate — returns the previously active arena (NULL if none).
void epub_arena_deactivate(void);

// Get the currently active parsing arena (NULL when inactive).
arena_t* epub_arena_active(void);

// Expat-compatible allocation functions (route to active arena with heap fallback).
void* epub_arena_malloc(size_t size);
void* epub_arena_realloc(void* ptr, size_t size);
void epub_arena_free(void* ptr);

// Convenience: duplicate a string into the active arena.
static inline char* epub_arena_strdup(const char* str) {
  if (!str) return NULL;
  arena_t* a = epub_arena_active();
  if (!a) return NULL;
  size_t len = strlen(str) + 1;
  char* dup = (char*)arena_alloc(a, len, 1);
  if (dup) memcpy(dup, str, len);
  return dup;
}

#ifdef __cplusplus
}
#endif
