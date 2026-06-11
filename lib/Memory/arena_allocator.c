#include "arena_allocator.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#else
#define ESP_LOGE(tag, fmt, ...)
#endif

void arena_init(arena_t* arena, void* buffer, size_t size, const char* name) {
    arena->buffer = (uint8_t*)buffer;
    arena->size = size;
    arena->offset = 0;
    arena->peak = 0;
    arena->name = name;
}

static inline size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

void* arena_alloc(arena_t* arena, size_t size, size_t alignment) {
    if (!arena || !arena->buffer || size == 0) return NULL;

    // Align current offset
    size_t aligned_offset = align_up(arena->offset, alignment);

    // Check if we have enough space
    if (aligned_offset + size > arena->size) {
        // Log error: arena name, requested size, remaining space
        ESP_LOGE("ARENA", "Arena '%s' out of memory: requested %zu, remaining %zu",
                 arena->name, size, arena->size - aligned_offset);
        return NULL;
    }

    void* ptr = arena->buffer + aligned_offset;
    arena->offset = aligned_offset + size;

    // Update peak usage
    if (arena->offset > arena->peak) {
        arena->peak = arena->offset;
    }

    return ptr;
}

void* arena_calloc(arena_t* arena, size_t count, size_t size) {
    size_t total = count * size;
    void* ptr = arena_alloc(arena, total, 1); // No alignment for memset
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void arena_reset(arena_t* arena) {
    if (arena) {
        arena->offset = 0;
    }
}

size_t arena_remaining(const arena_t* arena) {
    return arena ? (arena->size - arena->offset) : 0;
}

int arena_peak_usage(const arena_t* arena) {
    if (!arena || arena->size == 0) return 0;
    return (int)((arena->peak * 100) / arena->size);
}
