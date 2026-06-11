#pragma once

#include "epub_arena.h"
#include <expat.h>
#include <stdlib.h>
#include <string.h>

static void* expat_arena_malloc(size_t size) {
    if (global_epub_context) {
        size_t* p = (size_t*)epub_alloc_fallback(global_epub_context, size + sizeof(size_t), &global_epub_context->temp_arena);
        if (p) {
            *p = size;
            return p + 1;
        }
    }
    return malloc(size);
}

static void* expat_arena_realloc(void* ptr, size_t size) {
    if (!ptr) return expat_arena_malloc(size);
    if (size == 0) {
        return NULL; // we don't free in arenas
    }

    if (global_epub_context) {
        uint8_t* uptr = (uint8_t*)ptr;
        arena_t* arena = &global_epub_context->temp_arena;
        if (uptr >= arena->buffer && uptr < arena->buffer + arena->size) {
            size_t* old_p = (size_t*)ptr - 1;
            size_t old_size = *old_p;

            size_t* new_p = (size_t*)epub_alloc_fallback(global_epub_context, size + sizeof(size_t), &global_epub_context->temp_arena);
            if (new_p) {
                *new_p = size;
                memcpy(new_p + 1, ptr, old_size < size ? old_size : size);
                return new_p + 1;
            }
            return NULL;
        }

        arena = &global_epub_context->spine_arena;
        if (uptr >= arena->buffer && uptr < arena->buffer + arena->size) {
            size_t* old_p = (size_t*)ptr - 1;
            size_t old_size = *old_p;

            size_t* new_p = (size_t*)epub_alloc_fallback(global_epub_context, size + sizeof(size_t), &global_epub_context->temp_arena);
            if (new_p) {
                *new_p = size;
                memcpy(new_p + 1, ptr, old_size < size ? old_size : size);
                return new_p + 1;
            }
            return NULL;
        }
    }
    return realloc(ptr, size);
}

static void expat_arena_free(void* ptr) {
    if (!ptr) return;
    if (global_epub_context) {
        uint8_t* uptr = (uint8_t*)ptr;
        arena_t* arena = &global_epub_context->temp_arena;
        if (uptr >= arena->buffer && uptr < arena->buffer + arena->size) {
            return;
        }
        arena = &global_epub_context->spine_arena;
        if (uptr >= arena->buffer && uptr < arena->buffer + arena->size) {
            return;
        }
    }
    free(ptr);
}

static const XML_Memory_Handling_Suite expat_arena_mem_suite = {
    expat_arena_malloc,
    expat_arena_realloc,
    expat_arena_free
};
