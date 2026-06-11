#include "epub_arena.h"

#include <cstdlib>
#include <new>

// Global operator new/delete override.
// When the EPUB parse arena is active, allocations are routed there.
// When inactive, standard malloc/free is used directly (no header prefix).
//
// This captures std::string, std::vector, and shared_ptr control block
// allocations that happen during EPUB section parsing, eliminating heap
// fragmentation from the hundreds of small allocs that Expat callbacks trigger.
//
// Safety: epub_arena_free is a no-op for arena pointers, so destructors
// that run while the arena is still alive (or after deactivation but before
// destroy) work correctly — they just don't actually free anything.

void* operator new(size_t size) noexcept(false) {
  arena_t* arena = epub_arena_active();
  if (arena) {
    void* ptr = arena_alloc(arena, size, alignof(max_align_t));
    if (ptr) return ptr;
    // Arena exhausted — fall through to heap
  }
  void* ptr = std::malloc(size);
  if (!ptr) std::abort();
  return ptr;
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
  arena_t* arena = epub_arena_active();
  if (arena) {
    void* ptr = arena_alloc(arena, size, alignof(max_align_t));
    if (ptr) return ptr;
  }
  return std::malloc(size);
}

void* operator new[](size_t size) noexcept(false) {
  arena_t* arena = epub_arena_active();
  if (arena) {
    void* ptr = arena_alloc(arena, size, alignof(max_align_t));
    if (ptr) return ptr;
  }
  void* ptr = std::malloc(size);
  if (!ptr) std::abort();
  return ptr;
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  arena_t* arena = epub_arena_active();
  if (arena) {
    void* ptr = arena_alloc(arena, size, alignof(max_align_t));
    if (ptr) return ptr;
  }
  return std::malloc(size);
}

void operator delete(void* ptr) noexcept {
  if (!ptr) return;
  arena_t* arena = epub_arena_active();
  if (arena && arena_owns(arena, ptr)) return;
  std::free(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
  if (!ptr) return;
  arena_t* arena = epub_arena_active();
  if (arena && arena_owns(arena, ptr)) return;
  std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
  if (!ptr) return;
  arena_t* arena = epub_arena_active();
  if (arena && arena_owns(arena, ptr)) return;
  std::free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
  if (!ptr) return;
  arena_t* arena = epub_arena_active();
  if (arena && arena_owns(arena, ptr)) return;
  std::free(ptr);
}
