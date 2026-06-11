#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arena_t arena_t;

// Marker for save/restore (nested scratch scopes)
typedef struct {
  size_t offset;
} arena_marker_t;

/**
 * Create an arena with a fixed backing buffer size.
 * Allocates the backing buffer from the system heap.
 * Returns NULL if the system heap allocation fails.
 */
arena_t* arena_create(size_t capacity);

/**
 * Create an arena using an externally-provided buffer.
 * The arena struct itself is placed at the start of the buffer.
 * Useful for stack-allocated or statically-allocated arenas.
 */
arena_t* arena_create_from_buffer(void* buffer, size_t buffer_size);

/**
 * Allocate `size` bytes from the arena with `alignment` (must be power of 2).
 * Returns NULL if arena has insufficient remaining capacity.
 * Does NOT zero memory (use arena_alloc_zero for that).
 */
void* arena_alloc_aligned(arena_t* arena, size_t size, size_t alignment);

/**
 * Allocate `size` bytes with default alignment (4 bytes for ESP32-C3).
 */
void* arena_alloc(arena_t* arena, size_t size);

/**
 * Allocate and zero-initialize.
 */
void* arena_alloc_zero(arena_t* arena, size_t size);

/**
 * Reallocate memory within the arena.
 * If ptr is NULL, equivalent to arena_alloc(arena, new_size).
 * If ptr is the last allocation, extends in place if possible.
 * Otherwise, allocates new memory, copies old contents, and abandons old memory.
 */
void* arena_realloc(arena_t* arena, void* ptr, size_t old_size, size_t new_size);

/**
 * Duplicate a string into the arena.
 */
char* arena_strdup(arena_t* arena, const char* str);

/**
 * Duplicate n bytes of a string into the arena (null-terminated).
 */
char* arena_strndup(arena_t* arena, const char* str, size_t n);

/**
 * Printf into arena-allocated string.
 */
char* arena_sprintf(arena_t* arena, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

/**
 * Get a marker for the current allocation point.
 * Used for temporary "scratch" sub-scopes within the arena.
 */
arena_marker_t arena_mark(arena_t* arena);

/**
 * Reset arena to a previous marker, effectively freeing everything
 * allocated after that marker.
 */
void arena_reset_to_mark(arena_t* arena, arena_marker_t marker);

/**
 * Reset arena to empty (offset = 0). All prior allocations are invalidated.
 */
void arena_reset(arena_t* arena);

/**
 * Destroy arena and free the backing buffer (only if arena_create was used).
 */
void arena_destroy(arena_t* arena);

/**
 * Query functions for monitoring.
 */
size_t arena_capacity(const arena_t* arena);
size_t arena_used(const arena_t* arena);
size_t arena_remaining(const arena_t* arena);
size_t arena_peak_used(const arena_t* arena);  // high-water mark

#ifdef __cplusplus
}
#endif
