#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t* buffer;
  size_t capacity;
  size_t offset;
  size_t peak;
  const char* name;
  bool owns_buffer;
} arena_t;

typedef struct {
  size_t offset;
} arena_marker_t;

// Create arena with heap-allocated buffer. Returns false on OOM.
bool arena_create(arena_t* arena, size_t capacity, const char* name);

// Initialize arena with an externally-owned buffer (arena does not free it).
void arena_init_from_buffer(arena_t* arena, void* buffer, size_t size, const char* name);

// Allocate aligned memory. Returns NULL when exhausted.
void* arena_alloc(arena_t* arena, size_t size, size_t alignment);

// Allocate and zero-initialize.
void* arena_calloc(arena_t* arena, size_t count, size_t elem_size, size_t alignment);

// Reallocate. Extends in-place if ptr was the last allocation; otherwise copies.
void* arena_realloc(arena_t* arena, void* ptr, size_t old_size, size_t new_size, size_t alignment);

// Save/restore for scoped scratch allocations.
arena_marker_t arena_mark(const arena_t* arena);
void arena_reset_to_mark(arena_t* arena, arena_marker_t marker);

// Reset all allocations (offset = 0). Peak is preserved.
void arena_reset(arena_t* arena);

// Free the backing buffer (only if arena_create was used).
void arena_destroy(arena_t* arena);

// Query functions.
size_t arena_capacity(const arena_t* arena);
size_t arena_used(const arena_t* arena);
size_t arena_remaining(const arena_t* arena);
size_t arena_peak(const arena_t* arena);
int arena_peak_percent(const arena_t* arena);

// Check if a pointer belongs to this arena's buffer range.
bool arena_owns(const arena_t* arena, const void* ptr);

#define ARENA_ALLOC(arena, type) ((type*)arena_alloc(arena, sizeof(type), _Alignof(type)))
#define ARENA_ALLOC_ARRAY(arena, type, count) ((type*)arena_alloc(arena, (count) * sizeof(type), _Alignof(type)))

#ifdef __cplusplus
}
#endif
