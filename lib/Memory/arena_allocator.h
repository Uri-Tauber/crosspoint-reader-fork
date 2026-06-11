#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* buffer;      // Start of arena memory
    size_t size;          // Total arena size
    size_t offset;        // Current allocation offset
    size_t peak;          // Peak memory usage
    const char* name;     // Arena identifier for debugging
} arena_t;

// Initialize arena with existing buffer
void arena_init(arena_t* arena, void* buffer, size_t size, const char* name);

// Allocate aligned memory from arena
void* arena_alloc(arena_t* arena, size_t size, size_t alignment);

// Allocate and zero-initialize memory
void* arena_calloc(arena_t* arena, size_t count, size_t size);

// Reset arena (free all allocations)
void arena_reset(arena_t* arena);

// Get remaining space
size_t arena_remaining(const arena_t* arena);

// Get peak usage percentage (0-100)
int arena_peak_usage(const arena_t* arena);

// Helper macros for common uses
#define ARENA_ALLOC(arena, type) ((type*)arena_alloc(arena, sizeof(type), _Alignof(type)))
#define ARENA_ALLOC_ARRAY(arena, type, count) ((type*)arena_alloc(arena, (count) * sizeof(type), _Alignof(type)))

#ifdef __cplusplus
}
#endif
