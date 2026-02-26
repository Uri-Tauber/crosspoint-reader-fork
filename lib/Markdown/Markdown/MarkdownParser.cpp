#include "MarkdownParser.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "../md4c/md4c.h"
#include "Epub/Page.h"

namespace {
bool isWhitespaceChar(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

// Maximum file size we'll attempt to parse (512 KB).
// MD4C requires the entire file in contiguous memory.
constexpr size_t MAX_MARKDOWN_SIZE = 512 * 1024;
}  // namespace

EpdFontFamily::Style MarkdownParser::getCurrentFontStyle() const {
  if (boldDepth > 0 && italicDepth > 0) {
    return EpdFontFamily::BOLD_ITALIC;
  } else if (boldDepth > 0) {
    return EpdFontFamily::BOLD;
  } else if (italicDepth > 0) {
    return EpdFontFamily::ITALIC;
  }
  return EpdFontFamily::REGULAR;
}

void MarkdownParser::flushPartWordBuffer() {
  if (partWordBufferIndex > 0) {
    partWordBuffer[partWordBufferIndex] = '\0';
    if (currentTextBlock) {
      currentTextBlock->addWord(partWordBuffer, getCurrentFontStyle());
    }
    partWordBufferIndex = 0;
  }
}

void MarkdownParser::startNewTextBlock(const CssTextAlign alignment) {
  if (currentTextBlock) {
    if (currentTextBlock->isEmpty()) {
      BlockStyle bs;
      bs.alignment = alignment;
      bs.textAlignDefined = true;
      currentTextBlock->setBlockStyle(bs);
      return;
    }
    makePages();
  }

  BlockStyle blockStyle;
  blockStyle.alignment = alignment;
  blockStyle.textAlignDefined = true;
  currentTextBlock.reset(new ParsedText(extraParagraphSpacing, hyphenationEnabled, blockStyle));
}

void MarkdownParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

  if (currentPageNextY + lineHeight > viewportHeight) {
    completePageFn(std::move(currentPage));
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  const int16_t xOffset = line->getBlockStyle().leftInset();
  currentPage->elements.push_back(std::make_shared<PageLine>(line, xOffset, currentPageNextY));
  currentPageNextY += lineHeight;
}

void MarkdownParser::makePages() {
  if (!currentTextBlock) {
    LOG_ERR("MDP", "No text block to make pages for");
    return;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

  const BlockStyle& blockStyle = currentTextBlock->getBlockStyle();
  if (blockStyle.marginTop > 0) {
    currentPageNextY += blockStyle.marginTop;
  }

  const int horizontalInset = blockStyle.totalHorizontalInset();
  const uint16_t effectiveWidth =
      (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;

  currentTextBlock->layoutAndExtractLines(
      renderer, fontId, effectiveWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); });

  if (blockStyle.marginBottom > 0) {
    currentPageNextY += blockStyle.marginBottom;
  }

  if (extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}

int MarkdownParser::enterBlockCallback(int blockType, void* detail, void* userdata) {
  auto* self = static_cast<MarkdownParser*>(userdata);
  const auto userAlignment = static_cast<CssTextAlign>(self->paragraphAlignment);

  switch (static_cast<MD_BLOCKTYPE>(blockType)) {
    case MD_BLOCK_DOC:
      self->startNewTextBlock(userAlignment);
      break;

    case MD_BLOCK_H: {
      self->flushPartWordBuffer();
      auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
      self->headerLevel = h->level;
      self->startNewTextBlock(CssTextAlign::Center);
      self->boldDepth++;
      break;
    }

    case MD_BLOCK_P:
      self->flushPartWordBuffer();
      self->startNewTextBlock(userAlignment);
      break;

    case MD_BLOCK_QUOTE: {
      self->flushPartWordBuffer();
      BlockStyle quoteStyle;
      quoteStyle.alignment = CssTextAlign::Left;
      quoteStyle.textAlignDefined = true;
      quoteStyle.marginLeft = 10;
      if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
        self->makePages();
      }
      self->currentTextBlock.reset(
          new ParsedText(self->extraParagraphSpacing, self->hyphenationEnabled, quoteStyle));
      self->italicDepth++;
      break;
    }

    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
      break;

    case MD_BLOCK_LI: {
      self->flushPartWordBuffer();
      BlockStyle listStyle;
      listStyle.alignment = CssTextAlign::Left;
      listStyle.textAlignDefined = true;
      listStyle.marginLeft = 10;
      if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
        self->makePages();
      }
      self->currentTextBlock.reset(
          new ParsedText(self->extraParagraphSpacing, self->hyphenationEnabled, listStyle));
      self->inListItem = true;
      self->firstListItemWord = true;
      break;
    }

    case MD_BLOCK_CODE:
      self->flushPartWordBuffer();
      self->startNewTextBlock(CssTextAlign::Left);
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("[Code:", EpdFontFamily::ITALIC);
      }
      break;

    case MD_BLOCK_HR:
      self->flushPartWordBuffer();
      self->startNewTextBlock(CssTextAlign::Center);
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                                        "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80",
                                        EpdFontFamily::REGULAR);
      }
      break;

    case MD_BLOCK_TABLE:
      self->flushPartWordBuffer();
      self->startNewTextBlock(CssTextAlign::Center);
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("[Table", EpdFontFamily::ITALIC);
        self->currentTextBlock->addWord("omitted]", EpdFontFamily::ITALIC);
      }
      break;

    case MD_BLOCK_HTML:
      break;

    default:
      break;
  }

  return 0;
}

int MarkdownParser::leaveBlockCallback(int blockType, void* detail, void* userdata) {
  auto* self = static_cast<MarkdownParser*>(userdata);
  (void)detail;

  switch (static_cast<MD_BLOCKTYPE>(blockType)) {
    case MD_BLOCK_DOC:
      break;

    case MD_BLOCK_H:
      self->flushPartWordBuffer();
      if (self->boldDepth > 0) self->boldDepth--;
      self->headerLevel = 0;
      break;

    case MD_BLOCK_P:
    case MD_BLOCK_LI:
      self->flushPartWordBuffer();
      self->inListItem = false;
      self->firstListItemWord = false;
      break;

    case MD_BLOCK_QUOTE:
      self->flushPartWordBuffer();
      if (self->italicDepth > 0) self->italicDepth--;
      break;

    case MD_BLOCK_CODE:
      self->flushPartWordBuffer();
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("]", EpdFontFamily::ITALIC);
      }
      break;

    default:
      break;
  }

  return 0;
}

int MarkdownParser::enterSpanCallback(int spanType, void* detail, void* userdata) {
  auto* self = static_cast<MarkdownParser*>(userdata);
  (void)detail;

  switch (static_cast<MD_SPANTYPE>(spanType)) {
    case MD_SPAN_STRONG:
      self->boldDepth++;
      break;

    case MD_SPAN_EM:
      self->italicDepth++;
      break;

    case MD_SPAN_CODE:
      self->italicDepth++;
      break;

    case MD_SPAN_A:
      break;

    case MD_SPAN_IMG:
      self->flushPartWordBuffer();
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("[Image]", EpdFontFamily::ITALIC);
      }
      break;

    case MD_SPAN_DEL:
      break;

    default:
      break;
  }

  return 0;
}

int MarkdownParser::leaveSpanCallback(int spanType, void* detail, void* userdata) {
  auto* self = static_cast<MarkdownParser*>(userdata);
  (void)detail;

  switch (static_cast<MD_SPANTYPE>(spanType)) {
    case MD_SPAN_STRONG:
      if (self->boldDepth > 0) self->boldDepth--;
      break;

    case MD_SPAN_EM:
    case MD_SPAN_CODE:
      if (self->italicDepth > 0) self->italicDepth--;
      break;

    default:
      break;
  }

  return 0;
}

int MarkdownParser::textCallback(int textType, const char* text, unsigned size, void* userdata) {
  auto* self = static_cast<MarkdownParser*>(userdata);

  switch (static_cast<MD_TEXTTYPE>(textType)) {
    case MD_TEXT_BR:
    case MD_TEXT_SOFTBR:
      self->flushPartWordBuffer();
      return 0;

    case MD_TEXT_CODE:
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("...", EpdFontFamily::ITALIC);
      }
      return 0;

    case MD_TEXT_HTML:
      return 0;

    case MD_TEXT_ENTITY:
      if (size == 6 && strncmp(text, "&nbsp;", 6) == 0) {
        self->flushPartWordBuffer();
      } else if (self->partWordBufferIndex < MD_MAX_WORD_SIZE) {
        if (size == 6 && strncmp(text, "&quot;", 6) == 0) {
          self->partWordBuffer[self->partWordBufferIndex++] = '"';
        } else if (size == 5 && strncmp(text, "&amp;", 5) == 0) {
          self->partWordBuffer[self->partWordBufferIndex++] = '&';
        } else if (size == 4 && strncmp(text, "&lt;", 4) == 0) {
          self->partWordBuffer[self->partWordBufferIndex++] = '<';
        } else if (size == 4 && strncmp(text, "&gt;", 4) == 0) {
          self->partWordBuffer[self->partWordBufferIndex++] = '>';
        }
      }
      return 0;

    default:
      break;
  }

  // Add bullet for first word in list item
  if (self->firstListItemWord && self->inListItem) {
    if (self->currentTextBlock) {
      self->currentTextBlock->addWord("\xe2\x80\xa2", EpdFontFamily::REGULAR);
    }
    self->firstListItemWord = false;
  }

  EpdFontFamily::Style fontStyle = self->getCurrentFontStyle();

  for (unsigned i = 0; i < size; i++) {
    if (isWhitespaceChar(text[i])) {
      if (self->partWordBufferIndex > 0) {
        self->partWordBuffer[self->partWordBufferIndex] = '\0';
        if (self->currentTextBlock) {
          self->currentTextBlock->addWord(self->partWordBuffer, fontStyle);
        }
        self->partWordBufferIndex = 0;
      }
      continue;
    }

    if (self->partWordBufferIndex >= MD_MAX_WORD_SIZE) {
      self->partWordBuffer[self->partWordBufferIndex] = '\0';
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord(self->partWordBuffer, fontStyle);
      }
      self->partWordBufferIndex = 0;
    }

    self->partWordBuffer[self->partWordBufferIndex++] = text[i];
  }

  // If we have > 750 words buffered, perform layout to free memory
  if (self->currentTextBlock && self->currentTextBlock->size() > 750) {
    LOG_DBG("MDP", "Text block too long, splitting");
    const int horizontalInset = self->currentTextBlock->getBlockStyle().totalHorizontalInset();
    const uint16_t effectiveWidth = (horizontalInset < self->viewportWidth)
                                        ? static_cast<uint16_t>(self->viewportWidth - horizontalInset)
                                        : self->viewportWidth;
    self->currentTextBlock->layoutAndExtractLines(
        self->renderer, self->fontId, effectiveWidth,
        [self](const std::shared_ptr<TextBlock>& textBlock) { self->addLineToPage(textBlock); }, false);
  }

  return 0;
}

bool MarkdownParser::parseAndBuildPages() {
  if (!markdown || !markdown->getFileSize()) {
    LOG_ERR("MDP", "Markdown not loaded or empty");
    return false;
  }

  const size_t fileSize = markdown->getFileSize();
  if (fileSize > MAX_MARKDOWN_SIZE) {
    LOG_ERR("MDP", "File too large: %zu bytes (max %zu)", fileSize, MAX_MARKDOWN_SIZE);
    return false;
  }

  auto* buffer = static_cast<uint8_t*>(malloc(fileSize + 1));
  if (!buffer) {
    LOG_ERR("MDP", "Failed to allocate %zu bytes", fileSize);
    return false;
  }

  if (!markdown->readContent(buffer, 0, fileSize)) {
    LOG_ERR("MDP", "Failed to read file content");
    free(buffer);
    return false;
  }
  buffer[fileSize] = '\0';

  LOG_DBG("MDP", "Parsing %zu bytes of markdown", fileSize);

  // Initialize parser state
  boldDepth = 0;
  italicDepth = 0;
  headerLevel = 0;
  inListItem = false;
  firstListItemWord = false;
  partWordBufferIndex = 0;
  currentTextBlock.reset();
  currentPage.reset();
  currentPageNextY = 0;

  // Setup MD4C parser
  MD_PARSER parser = {};
  parser.abi_version = 0;
  parser.flags = MD_DIALECT_COMMONMARK;
  parser.enter_block = reinterpret_cast<int (*)(MD_BLOCKTYPE, void*, void*)>(enterBlockCallback);
  parser.leave_block = reinterpret_cast<int (*)(MD_BLOCKTYPE, void*, void*)>(leaveBlockCallback);
  parser.enter_span = reinterpret_cast<int (*)(MD_SPANTYPE, void*, void*)>(enterSpanCallback);
  parser.leave_span = reinterpret_cast<int (*)(MD_SPANTYPE, void*, void*)>(leaveSpanCallback);
  parser.text = reinterpret_cast<int (*)(MD_TEXTTYPE, const MD_CHAR*, MD_SIZE, void*)>(textCallback);
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  int result = md_parse(reinterpret_cast<const char*>(buffer), static_cast<unsigned>(fileSize), &parser, this);
  free(buffer);

  if (result != 0) {
    LOG_ERR("MDP", "md_parse failed with code %d", result);
    return false;
  }

  // Process remaining content
  flushPartWordBuffer();
  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    makePages();
  }
  if (currentPage) {
    completePageFn(std::move(currentPage));
  }

  LOG_DBG("MDP", "Parsing complete");
  return true;
}
