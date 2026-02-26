#pragma once

#include <HalStorage.h>

#include <functional>
#include <memory>
#include <string>

#include "../Markdown.h"

class Page;
class GfxRenderer;

class MarkdownSection {
  std::shared_ptr<Markdown> markdown;
  GfxRenderer& renderer;
  std::string filePath;
  FsFile file;

  void writeHeader(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                   uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled);
  uint32_t onPageComplete(std::unique_ptr<Page> page);

 public:
  uint16_t pageCount = 0;
  int currentPage = 0;

  explicit MarkdownSection(const std::shared_ptr<Markdown>& markdown, GfxRenderer& renderer)
      : markdown(markdown), renderer(renderer), filePath(markdown->getCachePath() + "/section.bin") {}

  bool loadSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                       uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled);
  bool clearCache() const;
  bool createSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                         uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                         const std::function<void()>& popupFn = nullptr);
  std::unique_ptr<Page> loadPageFromSectionFile();
};
