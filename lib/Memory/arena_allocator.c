#include "arena_allocator.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#define ARENA_LOGW(fmt, ...) ESP_LOGW("ARENA", fmt, ##__VA_ARGS__)
#define ARENA_LOGE(fmt, ...) ESP_LOGE("ARENA", fmt, ##__VA_ARGS__)
#else
#define ARENA_LOGW(fmt, ...)
#define ARENA_LOGE(fmt, ...)
#endif

static inline size_t align_up(size_t value, size_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

bool arena_create(arena_t* arena, size_t capacity, const char* name) {
  if (!arena || capacity == 0) return false;

  void* buf = malloc(capacity);
  if (!buf) {
    ARENA_LOGE("Failed to allocate %zu bytes for arena '%s'", capacity, name ? name : "?");
    return false;
  }

  arena->buffer = (uint8_t*)buf;
  arena->capacity = capacity;
  arena->offset = 0;
  arena->peak = 0;
  arena->name = name;
  arena->owns_buffer = true;
  return true;
}

void arena_init_from_buffer(arena_t* arena, void* buffer, size_t size, const char* name) {
  if (!arena) return;
  arena->buffer = (uint8_t*)buffer;
  arena->capacity = size;
  arena->offset = 0;
  arena->peak = 0;
  arena->name = name;
  arena->owns_buffer = false;
}

void* arena_alloc(arena_t* arena, size_t size, size_t alignment) {
  if (!arena || !arena->buffer || size == 0) return NULL;

  size_t aligned_offset = align_up(arena->offset, alignment);

  if (aligned_offset + size > arena->capacity || aligned_offset + size < aligned_offset) {
    ARENA_LOGW("'%s' exhausted: need %zu, have %zu", arena->name, size, arena->capacity - arena->offset);
    return NULL;
  }

  void* ptr = arena->buffer + aligned_offset;
  arena->offset = aligned_offset + size;

  if (arena->offset > arena->peak) {
    arena->peak = arena->offset;
  }

  return ptr;
}

void* arena_calloc(arena_t* arena, size_t count, size_t elem_size, size_t alignment) {
  size_t total = count * elem_size;
  void* ptr = arena_alloc(arena, total, alignment);
  if (ptr) {
    memset(ptr, 0, total);
  }
  return ptr;
}

void* arena_realloc(arena_t* arena, void* ptr, size_t old_size, size_t new_size, size_t alignment) {
  if (!arena) return NULL;
  if (!ptr) return arena_alloc(arena, new_size, alignment);
  if (new_size == 0) return NULL;

  // If ptr was the last allocation, extend or shrink in-place
  if ((uint8_t*)ptr + old_size == arena->buffer + arena->offset) {
    if (new_size <= old_size) {
      arena->offset -= (old_size - new_size);
      return ptr;
    }
    size_t additional = new_size - old_size;
    if (arena->offset + additional <= arena->capacity) {
      arena->offset += additional;
      if (arena->offset > arena->peak) {
        arena->peak = arena->offset;
      }
      return ptr;
    }
  }

  // General case: allocate new, copy old content
  void* new_ptr = arena_alloc(arena, new_size, alignment);
  if (new_ptr) {
    memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
  }
  return new_ptr;
}

arena_marker_t arena_mark(const arena_t* arena) {
  arena_marker_t m = {0};
  if (arena) {
    m.offset = arena->offset;
  }
  return m;
}

void arena_reset_to_mark(arena_t* arena, arena_marker_t marker) {
  if (arena && marker.offset <= arena->offset) {
    arena->offset = marker.offset;
  }
}

void arena_reset(arena_t* arena) {
  if (arena) {
    arena->offset = 0;
  }
}

void arena_destroy(arena_t* arena) {
  if (arena && arena->owns_buffer) {
    free(arena->buffer);
    arena->buffer = NULL;
    arena->capacity = 0;
    arena->offset = 0;
    arena->owns_buffer = false;
  }
}

size_t arena_capacity(const arena_t* arena) { return arena ? arena->capacity : 0; }
size_t arena_used(const arena_t* arena) { return arena ? arena->offset : 0; }
size_t arena_remaining(const arena_t* arena) { return arena ? (arena->capacity - arena->offset) : 0; }
size_t arena_peak(const arena_t* arena) { return arena ? arena->peak : 0; }

int arena_peak_percent(const arena_t* arena) {
  if (!arena || arena->capacity == 0) return 0;
  return (int)((arena->peak * 100) / arena->capacity);
}

bool arena_owns(const arena_t* arena, const void* ptr) {
  if (!arena || !arena->buffer || !ptr) return false;
  const uint8_t* p = (const uint8_t*)ptr;
  return p >= arena->buffer && p < arena->buffer + arena->capacity;
}
