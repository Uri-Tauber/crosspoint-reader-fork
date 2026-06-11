#include "arena.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// To allow compiling on a regular host system where ESP logging might be missing
#if __has_include("esp_heap_caps.h")
#include "esp_heap_caps.h"
#include "esp_log.h"
#else
#define ESP_LOGW(tag, fmt, ...) printf("WARN: [%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("ERR:  [%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

#define ARENA_DEFAULT_ALIGNMENT 4  // ESP32-C3 is 32-bit RISC-V
#define ARENA_TAG "arena"

struct arena_t {
  uint8_t* buffer;   // Start of usable memory
  size_t capacity;   // Total usable bytes
  size_t offset;     // Current bump pointer offset
  size_t peak;       // High-water mark
  bool owns_buffer;  // Whether arena_destroy should free the buffer
};

static inline size_t align_forward(size_t offset, size_t alignment) {
  return (offset + (alignment - 1)) & ~(alignment - 1);
}

arena_t* arena_create(size_t capacity) {
  // Allocate arena struct + buffer in one go
  size_t total_size = sizeof(arena_t) + capacity;
  void* mem = malloc(total_size);
  if (!mem) {
    ESP_LOGE(ARENA_TAG, "Failed to allocate arena of size %zu", total_size);
    return NULL;
  }

  arena_t* arena = (arena_t*)mem;
  arena->buffer = (uint8_t*)mem + sizeof(arena_t);
  arena->capacity = capacity;
  arena->offset = 0;
  arena->peak = 0;
  arena->owns_buffer = true;

  return arena;
}

arena_t* arena_create_from_buffer(void* buffer, size_t buffer_size) {
  if (!buffer || buffer_size < sizeof(arena_t)) {
    return NULL;
  }

  arena_t* arena = (arena_t*)buffer;
  arena->buffer = (uint8_t*)buffer + sizeof(arena_t);
  arena->capacity = buffer_size - sizeof(arena_t);
  arena->offset = 0;
  arena->peak = 0;
  arena->owns_buffer = false;

  return arena;
}

void* arena_alloc_aligned(arena_t* arena, size_t size, size_t alignment) {
  if (!arena) return NULL;

  size_t aligned_offset = align_forward(arena->offset, alignment);
  if (aligned_offset + size > arena->capacity || aligned_offset + size < aligned_offset) {
    ESP_LOGW(ARENA_TAG, "Arena exhausted: requested %zu, remaining %zu", size,
             arena->capacity > arena->offset ? arena->capacity - arena->offset : 0);
    return NULL;  // Caller MUST check
  }

  void* ptr = arena->buffer + aligned_offset;
  arena->offset = aligned_offset + size;
  if (arena->offset > arena->peak) {
    arena->peak = arena->offset;
  }
  return ptr;
}

void* arena_alloc(arena_t* arena, size_t size) { return arena_alloc_aligned(arena, size, ARENA_DEFAULT_ALIGNMENT); }

void* arena_alloc_zero(arena_t* arena, size_t size) {
  void* ptr = arena_alloc(arena, size);
  if (ptr) {
    memset(ptr, 0, size);
  }
  return ptr;
}

void* arena_realloc(arena_t* arena, void* ptr, size_t old_size, size_t new_size) {
  if (!arena) return NULL;

  // Check if this was the last allocation (common case for growing buffers)
  if (ptr && (uint8_t*)ptr + old_size == arena->buffer + arena->offset) {
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
      return ptr;  // Extended in-place
    }
  }

  // General case: allocate new, copy, waste old space
  void* new_ptr = arena_alloc(arena, new_size);
  if (new_ptr && ptr) {
    memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
  }
  return new_ptr;
}

char* arena_strdup(arena_t* arena, const char* str) {
  if (!arena || !str) return NULL;
  size_t len = strlen(str) + 1;
  char* copy = (char*)arena_alloc(arena, len);
  if (copy) {
    memcpy(copy, str, len);
  }
  return copy;
}

char* arena_strndup(arena_t* arena, const char* str, size_t n) {
  if (!arena || !str) return NULL;

  size_t len = 0;
  while (len < n && str[len] != '\0') {
    len++;
  }

  char* copy = (char*)arena_alloc(arena, len + 1);
  if (copy) {
    memcpy(copy, str, len);
    copy[len] = '\0';
  }
  return copy;
}

char* arena_sprintf(arena_t* arena, const char* fmt, ...) {
  if (!arena || !fmt) return NULL;

  va_list args;
  va_start(args, fmt);
  // First pass to determine length
  int len = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  if (len < 0) return NULL;

  char* buf = (char*)arena_alloc(arena, len + 1);
  if (buf) {
    va_start(args, fmt);
    vsnprintf(buf, len + 1, fmt, args);
    va_end(args);
  }
  return buf;
}

arena_marker_t arena_mark(arena_t* arena) {
  arena_marker_t marker = {0};
  if (arena) {
    marker.offset = arena->offset;
  }
  return marker;
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
    free(arena);
  }
}

size_t arena_capacity(const arena_t* arena) { return arena ? arena->capacity : 0; }

size_t arena_used(const arena_t* arena) { return arena ? arena->offset : 0; }

size_t arena_remaining(const arena_t* arena) { return arena ? (arena->capacity - arena->offset) : 0; }

size_t arena_peak_used(const arena_t* arena) { return arena ? arena->peak : 0; }
