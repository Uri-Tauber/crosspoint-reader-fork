#pragma once

class GfxRenderer;

typedef enum { TEXT_BLOCK, IMAGE_BLOCK } BlockType;

// a block of content in the html - either a paragraph or an image
#include "../parsers/ParseArena.h"
class Block {
 public:
  void* operator new(size_t size) { return parse_malloc(size); }
  void* operator new(size_t size, const std::nothrow_t&) noexcept { return parse_malloc(size); }
  void operator delete(void* ptr) { parse_free(ptr); }
  void operator delete(void* ptr, const std::nothrow_t&) noexcept { parse_free(ptr); }

  virtual ~Block() = default;

  virtual BlockType getType() = 0;
  virtual bool isEmpty() = 0;
  virtual void finish() {}
};
