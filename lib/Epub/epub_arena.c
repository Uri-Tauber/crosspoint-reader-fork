#include "epub_arena.h"

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

static arena_t* g_active_arena = NULL;

void epub_arena_activate(arena_t* arena) { g_active_arena = arena; }

void epub_arena_deactivate(void) { g_active_arena = NULL; }

arena_t* epub_arena_active(void) { return g_active_arena; }

// Each allocation is prefixed with its size so realloc can copy the right amount.
// Layout: [size_t old_size][payload...]
//         ^returned pointer points here

void* epub_arena_malloc(size_t size) {
  if (g_active_arena) {
    size_t total = size + sizeof(size_t);
    size_t* hdr = (size_t*)arena_alloc(g_active_arena, total, sizeof(size_t));
    if (hdr) {
      *hdr = size;
      return hdr + 1;
    }
    EARENA_LOGW("Arena full, heap fallback for %zu bytes", size);
  }
  // Heap fallback — also prefixed so realloc/free can detect ownership
  size_t* hdr = (size_t*)malloc(size + sizeof(size_t));
  if (hdr) {
    *hdr = size;
    return hdr + 1;
  }
  return NULL;
}

void* epub_arena_realloc(void* ptr, size_t size) {
  if (!ptr) return epub_arena_malloc(size);
  if (size == 0) return NULL;

  size_t* old_hdr = (size_t*)ptr - 1;
  size_t old_size = *old_hdr;

  if (g_active_arena && arena_owns(g_active_arena, old_hdr)) {
    // Try to extend in the arena
    size_t total = size + sizeof(size_t);
    size_t old_total = old_size + sizeof(size_t);
    size_t* new_hdr = (size_t*)arena_realloc(g_active_arena, old_hdr, old_total, total, sizeof(size_t));
    if (new_hdr) {
      *new_hdr = size;
      return new_hdr + 1;
    }
    EARENA_LOGW("Arena realloc failed for %zu bytes, heap fallback", size);
  }

  if (!g_active_arena || !arena_owns(g_active_arena, old_hdr)) {
    // Pointer is on the heap — use standard realloc
    size_t* new_hdr = (size_t*)realloc(old_hdr, size + sizeof(size_t));
    if (new_hdr) {
      *new_hdr = size;
      return new_hdr + 1;
    }
    return NULL;
  }

  // Arena realloc failed — allocate on heap and copy
  size_t* new_hdr = (size_t*)malloc(size + sizeof(size_t));
  if (new_hdr) {
    *new_hdr = size;
    memcpy(new_hdr + 1, ptr, old_size < size ? old_size : size);
    return new_hdr + 1;
  }
  return NULL;
}

void epub_arena_free(void* ptr) {
  if (!ptr) return;

  size_t* hdr = (size_t*)ptr - 1;

  // If it's in the arena, no-op (bulk freed on arena reset/destroy)
  if (g_active_arena && arena_owns(g_active_arena, hdr)) {
    return;
  }

  // Otherwise it's a heap pointer — free it
  free(hdr);
}
