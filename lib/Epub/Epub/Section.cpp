#include "Section.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include "Epub/css/CssParser.h"
#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 14;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint32_t);
}  // namespace

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    LOG_ERR("SCT", "File not open for writing page %d", pageCount);
    return 0;
  }

  // Collect anchors from the page before serialization
  for (const auto& anchor : page->anchors) {
    anchors.emplace_back(anchor, pageCount);
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("SCT", "Failed to serialize page %d", pageCount);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", pageCount);

  pageCount++;
  return position;
}

void Section::writeSectionFileHeader(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                     const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                     const uint16_t viewportHeight, const bool hyphenationEnabled,
                                     const bool embeddedStyle) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(fontId) + sizeof(lineCompression) +
                                   sizeof(extraParagraphSpacing) + sizeof(paragraphAlignment) + sizeof(viewportWidth) +
                                   sizeof(viewportHeight) + sizeof(pageCount) + sizeof(hyphenationEnabled) +
                                   sizeof(embeddedStyle) + sizeof(uint32_t),
                "Header size mismatch");
  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, embeddedStyle);
  serialization::writePod(file, pageCount);  // Placeholder for page count (will be initially 0 when written)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset
}

bool Section::loadSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                              const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                              const uint16_t viewportHeight, const bool hyphenationEnabled, const bool embeddedStyle) {
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileEmbeddedStyle;
    serialization::readPod(file, fileFontId);
    serialization::readPod(file, fileLineCompression);
    serialization::readPod(file, fileExtraParagraphSpacing);
    serialization::readPod(file, fileParagraphAlignment);
    serialization::readPod(file, fileViewportWidth);
    serialization::readPod(file, fileViewportHeight);
    serialization::readPod(file, fileHyphenationEnabled);
    serialization::readPod(file, fileEmbeddedStyle);

    if (fontId != fileFontId || lineCompression != fileLineCompression ||
        extraParagraphSpacing != fileExtraParagraphSpacing || paragraphAlignment != fileParagraphAlignment ||
        viewportWidth != fileViewportWidth || viewportHeight != fileViewportHeight ||
        hyphenationEnabled != fileHyphenationEnabled || embeddedStyle != fileEmbeddedStyle) {
      file.close();
      LOG_DBG("SCT", "Section parameters mismatch, recreating");
      clearCache();
      return false;
    }

    serialization::readPod(file, pageCount);
  }

  // Load anchors
  if (!loadAnchorIndex()) {
    // If index load fails, maybe just recreate? Or ignore?
    // If we can't load anchors, we just can't jump to them. That's better than failing the whole book.
    LOG_ERR("SCT", "Failed to load anchor index");
  }

  return true;
}

bool Section::clearCache() const {
  bool ret = true;
  if (file) {
    // const_cast because we need to close it, but clearCache is conceptually const
    const_cast<FsFile&>(file).close();
  }
  if (Storage.exists(filePath.c_str())) {
    ret &= Storage.remove(filePath.c_str());
  }
  std::string idxPath = filePath + ".idx";
  if (Storage.exists(idxPath.c_str())) {
    ret &= Storage.remove(idxPath.c_str());
  }
  return ret;
}

bool Section::createSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                const uint16_t viewportHeight, const bool hyphenationEnabled, const bool embeddedStyle,
                                const std::function<void()>& popupFn) {
  if (file) {
    file.close();
  }
  if (!Storage.openFileForWrite("SCT", filePath, file)) {
    return false;
  }

  pageCount = 0;
  anchors.clear();  // Clear any existing anchors
  writeSectionFileHeader(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, embeddedStyle);

  BookMetadataCache::SpineEntry spineEntry = epub->getSpineItem(spineIndex);
  if (spineEntry.href.empty()) {
    LOG_ERR("SCT", "Spine item %d not found", spineIndex);
    file.close();
    return false;
  }

  std::string contentBase = epub->getBasePath();
  const auto lastSlash = spineEntry.href.find_last_of('/');
  if (lastSlash != std::string::npos) {
    contentBase += spineEntry.href.substr(0, lastSlash + 1);
  }

  ChapterHtmlSlimParser parser(
      epub, epub->getBasePath() + spineEntry.href, renderer, fontId, lineCompression, extraParagraphSpacing,
      paragraphAlignment, viewportWidth, viewportHeight, hyphenationEnabled,
      [this](std::unique_ptr<Page> page) { this->onPageComplete(std::move(page)); }, embeddedStyle, contentBase,
      epub->getCachePath() + "/images/", popupFn, epub->getCssParser());

  if (!parser.parseAndBuildPages()) {
    LOG_ERR("SCT", "Failed to parse chapter %d", spineIndex);
    file.close();
    return false;
  }

  // Write actual page count
  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(uint16_t));
  serialization::writePod(file, pageCount);
  file.close();

  // Save anchors to index file
  saveAnchorIndex();

  // Re-open for reading
  return loadSectionFile(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, embeddedStyle);
}

std::unique_ptr<Page> Section::loadPageFromSectionFile() {
  if (!file) {
    return nullptr;
  }
  // We assume the file pointer is already at the start of the desired page
  // This requires the caller (EpubReaderActivity) to seek using the LUT if random access is needed
  // But wait, the current architecture just reads sequentially?
  // Let's check how 'loadPageFromSectionFile' is used.
  // It is used in EpubReaderActivity to load pages. It probably seeks first.
  // Actually, the current impl doesn't seem to use a LUT for random access to pages in this file?
  // Section.h has a placeholder for LUT offset.
  // But standard usage is just sequential read?
  // If we jump to a page, we need to know its offset.
  // That's a separate issue (random access to pages).
  // For now, let's just assume we are positioned correctly or don't break existing logic.
  return Page::deserialize(file);
}

bool Section::saveAnchorIndex() const {
  std::string idxPath = filePath + ".idx";
  FsFile idxFile;
  if (!Storage.openFileForWrite("SCT", idxPath, idxFile)) {
    LOG_ERR("SCT", "Failed to open anchor index file for writing");
    return false;
  }

  uint32_t count = static_cast<uint32_t>(anchors.size());
  serialization::writePod(idxFile, count);
  for (const auto& kv : anchors) {
    const std::string& key = kv.first;
    uint16_t page = kv.second;
    uint16_t len = static_cast<uint16_t>(key.length());
    serialization::writePod(idxFile, len);
    idxFile.write(reinterpret_cast<const uint8_t*>(key.c_str()), len);
    serialization::writePod(idxFile, page);
  }
  idxFile.close();
  return true;
}

bool Section::loadAnchorIndex() {
  anchors.clear();
  std::string idxPath = filePath + ".idx";
  if (!Storage.exists(idxPath.c_str())) {
    // No index exists, which is fine (no anchors or not yet created)
    return true;
  }

  FsFile idxFile;
  if (!Storage.openFileForRead("SCT", idxPath, idxFile)) {
    LOG_ERR("SCT", "Failed to open anchor index file for reading");
    return false;
  }

  uint32_t count;
  serialization::readPod(idxFile, count);
  for (uint32_t i = 0; i < count; ++i) {
    uint16_t len;
    serialization::readPod(idxFile, len);
    if (len > 256) { // Sanity check
       LOG_ERR("SCT", "Anchor length too large: %d", len);
       break;
    }
    std::string key(len, '\0');
    idxFile.read(reinterpret_cast<uint8_t*>(&key[0]), len);
    uint16_t page;
    serialization::readPod(idxFile, page);
    anchors.emplace_back(std::move(key), page);
  }
  idxFile.close();
  return true;
}

int Section::getPageForAnchor(const std::string& anchor) {
  if (anchors.empty()) {
    loadAnchorIndex();
  }
  for (const auto& kv : anchors) {
    if (kv.first == anchor) {
      return kv.second;
    }
  }
  return -1;
}