#pragma once
#include "../Memory/arena_allocator.h"
#include <esp_err.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// EPUB parsing context with multiple arenas
typedef struct {
    arena_t spine_arena;      // DOM, text, metadata for current spine item
    arena_t image_arena;      // Image decoding buffers
    arena_t temp_arena;       // Temporary parsing buffers
    bool is_initialized;
} epub_parsing_context_t;

extern epub_parsing_context_t* global_epub_context;

// Maximum sizes (adjust based on ESP32-C3 memory)
#define SPINE_ARENA_SIZE  (64 * 1024)  // 64KB per spine item
#define IMAGE_ARENA_SIZE  (32 * 1024)  // 32KB for image processing
#define TEMP_ARENA_SIZE   (16 * 1024)  // 16KB temporary

// Initialize EPUB parsing context from main heap
int epub_arenas_init(epub_parsing_context_t* ctx);

// Clean up all arenas
void epub_arenas_cleanup(epub_parsing_context_t* ctx);

// Reset for new spine item (keep memory, reset offsets)
void epub_arenas_reset_for_spine(epub_parsing_context_t* ctx);

// Diagnostic functions
void epub_arenas_dump_stats(const epub_parsing_context_t* ctx);
size_t epub_arenas_total_allocated(const epub_parsing_context_t* ctx);

void* epub_alloc_fallback(epub_parsing_context_t* ctx,
                          size_t size,
                          arena_t* preferred_arena);

static inline char* arena_strdup(arena_t* arena, const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* dup = (char*)arena_alloc(arena, len, 1);
    if (dup) memcpy(dup, str, len);
    return dup;
}

#define PARSER_ALLOC(ctx, type) ((type*)epub_alloc_fallback(ctx, sizeof(type), &(ctx)->spine_arena))
#define PARSER_ALLOC_ARRAY(ctx, type, count) ((type*)epub_alloc_fallback(ctx, (count) * sizeof(type), &(ctx)->spine_arena))
#define PARSER_STRDUP(ctx, str) arena_strdup(&(ctx)->spine_arena, str)

#ifdef __cplusplus
}
#endif
