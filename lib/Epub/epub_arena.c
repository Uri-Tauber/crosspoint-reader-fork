#include "epub_arena.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#define EARENA_LOGW(fmt, ...) ESP_LOGW("EPUB-Arena", fmt, ##__VA_ARGS__)
#define EARENA_LOGI(fmt, ...) ESP_LOGI("EPUB-Arena", fmt, ##__VA_ARGS__)
#else
#define EARENA_LOGW(fmt, ...)
#define EARENA_LOGI(fmt, ...)
#endif

// Header stored before each payload. Padded so the payload starts at max_align_t alignment.
// On ESP32-C3: sizeof(size_t) = 4, alignof(max_align_t) = 8, so HEADER_SIZE = 8.
#define PAYLOAD_ALIGN _Alignof(max_align_t)
#define HEADER_SIZE ((sizeof(size_t) + PAYLOAD_ALIGN - 1) & ~(PAYLOAD_ALIGN - 1))

static inline void* payload_from_base(void* base) { return (uint8_t*)base + HEADER_SIZE; }

static inline void* base_from_payload(void* payload) { return (uint8_t*)payload - HEADER_SIZE; }

static inline void header_write(void* base, size_t size) { *(size_t*)base = size; }

static inline size_t header_read(void* base) { return *(size_t*)base; }

static arena_t* g_active_arena = NULL;

void epub_arena_activate(arena_t* arena) { g_active_arena = arena; }

void epub_arena_deactivate(void) { g_active_arena = NULL; }

arena_t* epub_arena_active(void) { return g_active_arena; }

void* epub_arena_malloc(size_t size) {
  if (g_active_arena) {
    size_t total = HEADER_SIZE + size;
    void* base = arena_alloc(g_active_arena, total, PAYLOAD_ALIGN);
    if (base) {
      header_write(base, size);
      return payload_from_base(base);
    }
    EARENA_LOGW("Arena full, heap fallback for %zu bytes", size);
  }
  void* base = malloc(HEADER_SIZE + size);
  if (base) {
    header_write(base, size);
    return payload_from_base(base);
  }
  return NULL;
}

void* epub_arena_realloc(void* ptr, size_t size) {
  if (!ptr) return epub_arena_malloc(size);
  if (size == 0) return NULL;

  void* old_base = base_from_payload(ptr);
  size_t old_size = header_read(old_base);

  if (g_active_arena && arena_owns(g_active_arena, old_base)) {
    size_t total = HEADER_SIZE + size;
    size_t old_total = HEADER_SIZE + old_size;
    void* new_base = arena_realloc(g_active_arena, old_base, old_total, total, PAYLOAD_ALIGN);
    if (new_base) {
      header_write(new_base, size);
      return payload_from_base(new_base);
    }
    EARENA_LOGW("Arena realloc failed for %zu bytes, heap fallback", size);
    // Arena realloc failed — allocate on heap and copy
    void* heap_base = malloc(HEADER_SIZE + size);
    if (heap_base) {
      header_write(heap_base, size);
      memcpy(payload_from_base(heap_base), ptr, old_size < size ? old_size : size);
      return payload_from_base(heap_base);
    }
    return NULL;
  }

  // Pointer is on the heap — use standard realloc
  void* new_base = realloc(old_base, HEADER_SIZE + size);
  if (new_base) {
    header_write(new_base, size);
    return payload_from_base(new_base);
  }
  return NULL;
}

void epub_arena_free(void* ptr) {
  if (!ptr) return;

  void* base = base_from_payload(ptr);

  if (g_active_arena && arena_owns(g_active_arena, base)) {
    return;
  }

  free(base);
}
