#include "epub_arena.h"
#include <stdlib.h>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#else
#define ESP_LOGI(tag, fmt, ...)
#define ESP_LOGD(tag, fmt, ...)
#define ESP_LOGE(tag, fmt, ...)
#define ESP_LOGW(tag, fmt, ...)
#endif

#ifndef ESP_FAIL
#define ESP_FAIL -1
#endif
#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM 257
#endif

static const char* TAG = "EPUB-Arena";

epub_parsing_context_t* global_epub_context = NULL;

int epub_arenas_init(epub_parsing_context_t* ctx) {
    if (!ctx) return ESP_FAIL;

    // Allocate buffers from main heap
    void* spine_buf = malloc(SPINE_ARENA_SIZE);
    void* image_buf = malloc(IMAGE_ARENA_SIZE);
    void* temp_buf = malloc(TEMP_ARENA_SIZE);

    if (!spine_buf || !image_buf || !temp_buf) {
        free(spine_buf);
        free(image_buf);
        free(temp_buf);
        ESP_LOGE(TAG, "Failed to allocate arena buffers");
        return ESP_ERR_NO_MEM;
    }

    arena_init(&ctx->spine_arena, spine_buf, SPINE_ARENA_SIZE, "spine");
    arena_init(&ctx->image_arena, image_buf, IMAGE_ARENA_SIZE, "image");
    arena_init(&ctx->temp_arena, temp_buf, TEMP_ARENA_SIZE, "temp");
    ctx->is_initialized = true;

    ESP_LOGI(TAG, "Arenas initialized: spine=%dKB, image=%dKB, temp=%dKB",
             SPINE_ARENA_SIZE/1024, IMAGE_ARENA_SIZE/1024, TEMP_ARENA_SIZE/1024);
    return ESP_OK;
}

void epub_arenas_cleanup(epub_parsing_context_t* ctx) {
    if (ctx && ctx->is_initialized) {
        free(ctx->spine_arena.buffer);
        free(ctx->image_arena.buffer);
        free(ctx->temp_arena.buffer);
        ctx->is_initialized = false;
        ESP_LOGI(TAG, "Arenas cleaned up");
    }
}

void epub_arenas_reset_for_spine(epub_parsing_context_t* ctx) {
    if (ctx && ctx->is_initialized) {
        arena_reset(&ctx->spine_arena);
        arena_reset(&ctx->image_arena);
        arena_reset(&ctx->temp_arena);
        ESP_LOGD(TAG, "Arenas reset for new spine item");
    }
}

void epub_arenas_dump_stats(const epub_parsing_context_t* ctx) {
    if (!ctx || !ctx->is_initialized) return;

    ESP_LOGI(TAG, "=== EPUB Arena Statistics ===");
    ESP_LOGI(TAG, "Spine arena: %zu/%zu bytes (%d%% peak)",
             ctx->spine_arena.offset, ctx->spine_arena.size,
             arena_peak_usage(&ctx->spine_arena));
    ESP_LOGI(TAG, "Image arena: %zu/%zu bytes (%d%% peak)",
             ctx->image_arena.offset, ctx->image_arena.size,
             arena_peak_usage(&ctx->image_arena));
    ESP_LOGI(TAG, "Temp arena: %zu/%zu bytes (%d%% peak)",
             ctx->temp_arena.offset, ctx->temp_arena.size,
             arena_peak_usage(&ctx->temp_arena));
}

size_t epub_arenas_total_allocated(const epub_parsing_context_t* ctx) {
    if (!ctx || !ctx->is_initialized) return 0;
    return ctx->spine_arena.offset + ctx->image_arena.offset + ctx->temp_arena.offset;
}

void* epub_alloc_fallback(epub_parsing_context_t* ctx,
                          size_t size,
                          arena_t* preferred_arena) {
    void* ptr = arena_alloc(preferred_arena, size, 8);

    if (!ptr) {
        // Arena full, fall back to heap
        ESP_LOGW(TAG, "Arena %s full, falling back to heap for %zu bytes",
                 preferred_arena->name, size);
        ptr = malloc(size);

        if (!ptr) {
            // Last resort: try other arenas
            if (preferred_arena != &ctx->temp_arena) {
                ptr = arena_alloc(&ctx->temp_arena, size, 8);
            }
            if (!ptr && preferred_arena != &ctx->image_arena) {
                ptr = arena_alloc(&ctx->image_arena, size, 8);
            }
        }
    }

    return ptr;
}
