#include "Section.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include "Epub/Page.h"
#include "MarkdownParser.h"

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 1;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(uint16_t) +
                                 sizeof(uint32_t);
}  // namespace

uint32_t MarkdownSection::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    LOG_ERR("MDS", "File not open for writing page %d", pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("MDS", "Failed to serialize page %d", pageCount);
    return 0;
  }
  LOG_DBG("MDS", "Page %d processed", pageCount);

  pageCount++;
  return position;
}

void MarkdownSection::writeHeader(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                  const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                  const uint16_t viewportHeight, const bool hyphenationEnabled) {
  if (!file) {
    LOG_ERR("MDS", "File not open for writing header");
    return;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(fontId) + sizeof(lineCompression) +
                                   sizeof(extraParagraphSpacing) + sizeof(paragraphAlignment) + sizeof(viewportWidth) +
                                   sizeof(viewportHeight) + sizeof(hyphenationEnabled) + sizeof(pageCount) +
                                   sizeof(uint32_t),
                "Header size mismatch");
  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, pageCount);
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset
}

bool MarkdownSection::loadSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                      const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                      const uint16_t viewportHeight, const bool hyphenationEnabled) {
  if (!Storage.openFileForRead("MDS", filePath, file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != SECTION_FILE_VERSION) {
    file.close();
    LOG_ERR("MDS", "Version mismatch: %u", version);
    clearCache();
    return false;
  }

  int fileFontId;
  float fileLineCompression;
  bool fileExtraParagraphSpacing;
  uint8_t fileParagraphAlignment;
  uint16_t fileViewportWidth, fileViewportHeight;
  bool fileHyphenationEnabled;

  serialization::readPod(file, fileFontId);
  serialization::readPod(file, fileLineCompression);
  serialization::readPod(file, fileExtraParagraphSpacing);
  serialization::readPod(file, fileParagraphAlignment);
  serialization::readPod(file, fileViewportWidth);
  serialization::readPod(file, fileViewportHeight);
  serialization::readPod(file, fileHyphenationEnabled);

  if (fontId != fileFontId || lineCompression != fileLineCompression ||
      extraParagraphSpacing != fileExtraParagraphSpacing || paragraphAlignment != fileParagraphAlignment ||
      viewportWidth != fileViewportWidth || viewportHeight != fileViewportHeight ||
      hyphenationEnabled != fileHyphenationEnabled) {
    file.close();
    LOG_ERR("MDS", "Parameters do not match cached values");
    clearCache();
    return false;
  }

  serialization::readPod(file, pageCount);
  file.close();
  LOG_DBG("MDS", "Loaded cached section: %d pages", pageCount);
  return true;
}

bool MarkdownSection::clearCache() const {
  if (!Storage.exists(filePath.c_str())) {
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("MDS", "Failed to clear cache");
    return false;
  }

  LOG_DBG("MDS", "Cache cleared");
  return true;
}

bool MarkdownSection::createSectionFile(const int fontId, const float lineCompression,
                                        const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
                                        const uint16_t viewportWidth, const uint16_t viewportHeight,
                                        const bool hyphenationEnabled, const std::function<void()>& popupFn) {
  markdown->setupCacheDir();

  if (popupFn) {
    popupFn();
  }

  if (!Storage.openFileForWrite("MDS", filePath, file)) {
    return false;
  }
  writeHeader(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth, viewportHeight,
              hyphenationEnabled);
  std::vector<uint32_t> lut;

  MarkdownParser parser(markdown, renderer, fontId, lineCompression, extraParagraphSpacing, paragraphAlignment,
                        viewportWidth, viewportHeight, hyphenationEnabled,
                        [this, &lut](std::unique_ptr<Page> page) { lut.emplace_back(onPageComplete(std::move(page))); });
  bool success = parser.parseAndBuildPages();

  if (!success) {
    LOG_ERR("MDS", "Failed to parse markdown");
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  const uint32_t lutOffset = file.position();
  bool hasFailedLutRecords = false;

  for (const uint32_t& pos : lut) {
    if (pos == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(file, pos);
  }

  if (hasFailedLutRecords) {
    LOG_ERR("MDS", "Invalid page positions in LUT");
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  // Write page count and LUT offset back into header
  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  file.close();
  return true;
}

std::unique_ptr<Page> MarkdownSection::loadPageFromSectionFile() {
  if (!Storage.openFileForRead("MDS", filePath, file)) {
    return nullptr;
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  file.seek(lutOffset + sizeof(uint32_t) * currentPage);
  uint32_t pagePos;
  serialization::readPod(file, pagePos);
  file.seek(pagePos);

  auto page = Page::deserialize(file);
  file.close();
  return page;
}
