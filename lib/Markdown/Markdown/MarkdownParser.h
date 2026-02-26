#pragma once

#include <functional>
#include <memory>
#include <string>

#include "../Markdown.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/blocks/TextBlock.h"

class Page;
class GfxRenderer;

constexpr int MD_MAX_WORD_SIZE = 200;

class MarkdownParser {
  const std::shared_ptr<Markdown>& markdown;
  GfxRenderer& renderer;
  std::function<void(std::unique_ptr<Page>)> completePageFn;

  // Reader settings
  int fontId;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  float lineCompression;
  bool extraParagraphSpacing;
  uint8_t paragraphAlignment;
  bool hyphenationEnabled;

  // Parsing state
  int boldDepth = 0;
  int italicDepth = 0;
  int headerLevel = 0;
  bool inListItem = false;
  bool firstListItemWord = false;

  // Word buffer
  char partWordBuffer[MD_MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;

  // Current text block and page being built
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;

  // MD4C callbacks
  static int enterBlockCallback(int blockType, void* detail, void* userdata);
  static int leaveBlockCallback(int blockType, void* detail, void* userdata);
  static int enterSpanCallback(int spanType, void* detail, void* userdata);
  static int leaveSpanCallback(int spanType, void* detail, void* userdata);
  static int textCallback(int textType, const char* text, unsigned size, void* userdata);

  // Page building
  void startNewTextBlock(CssTextAlign alignment);
  void makePages();
  void addLineToPage(std::shared_ptr<TextBlock> line);
  void flushPartWordBuffer();
  EpdFontFamily::Style getCurrentFontStyle() const;

 public:
  explicit MarkdownParser(const std::shared_ptr<Markdown>& markdown, GfxRenderer& renderer, int fontId,
                          float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                          uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                          const std::function<void(std::unique_ptr<Page>)>& completePageFn)
      : markdown(markdown),
        renderer(renderer),
        completePageFn(completePageFn),
        fontId(fontId),
        viewportWidth(viewportWidth),
        viewportHeight(viewportHeight),
        lineCompression(lineCompression),
        extraParagraphSpacing(extraParagraphSpacing),
        paragraphAlignment(paragraphAlignment),
        hyphenationEnabled(hyphenationEnabled) {}

  bool parseAndBuildPages();
};
