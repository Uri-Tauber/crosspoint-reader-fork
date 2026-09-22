#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Serialization.h>
#include <Utf8.h>

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 3;          // Increment when cache format changes
constexpr uint32_t INDEX_LOG_INTERVAL = 1000;
}  // namespace

void TxtReaderActivity::onEnter() {
  openStartedMs = millis();
  pageBuffer = makeUniqueNoThrow<uint8_t[]>(PAGE_BUFFER_SIZE);
  if (!pageBuffer) {
    fallbackIndex = true;
    LOG_ERR("TRS", "OOM: %u-byte TXT page buffer; using blocking index", static_cast<unsigned>(PAGE_BUFFER_SIZE));
  }
  ReaderActivity::onEnter();
}

void TxtReaderActivity::onExit() {
  pageIndex.close();
  pageBuffer.reset();
  ReaderActivity::onExit();
}

bool TxtReaderActivity::loadBook() {
  txt = makeUniqueNoThrow<Txt>(bookPath, "/.crosspoint");
  if (!txt) {
    LOG_ERR("TRS", "Failed to allocate TXT object");
    return false;
  }
  if (!txt->load()) {
    LOG_ERR("TRS", "Failed to load TXT");
    return false;
  }
  txt->setupCacheDir();
  return true;
}

void TxtReaderActivity::initializeReader(GfxRenderer& renderer) {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  if (txt->getFileSize() > UINT32_MAX) {
    LOG_ERR("TRS", "TXT file exceeds the 32-bit page-index format");
    fallbackIndex = true;
  }

  if (!fallbackIndex) {
    const TxtIndexSpec spec{static_cast<uint32_t>(txt->getFileSize()),
                            viewportWidth,
                            linesPerPage,
                            cachedFontId,
                            cachedScreenMargin,
                            cachedParagraphAlignment};
    if (!pageIndex.openOrStart(txt->getCachePath().c_str(), spec)) {
      LOG_ERR("TRS", "Incremental TXT index unavailable; using blocking index");
      fallbackIndex = true;
    } else if (pageIndex.isComplete()) {
      totalPages = pageIndex.knownPageCount();
      loadProgress();
    } else {
      totalPages = 0;
      nextIndexOffset = 0;
      indexStartedMs = millis();
      if (txt->getFileSize() == 0 && !pageIndex.finish()) {
        fallbackIndex = true;
      }
    }
  }

  if (fallbackIndex) useBlockingFallback(renderer);

  initialized = true;
}

void TxtReaderActivity::useBlockingFallback(GfxRenderer& renderer) {
  pageIndex.close();
  if (!loadPageIndexCache()) {
    buildPageIndexBlocking(renderer);
    savePageIndexCache();
  }
  loadProgress();
}

void TxtReaderActivity::buildPageIndexBlocking(GfxRenderer& renderer) {
  pageOffsets.clear();
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(renderer, offset, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
}

bool TxtReaderActivity::loadPageAtOffset(const GfxRenderer& renderer, size_t offset, std::vector<std::string>& outLines,
                                         size_t& nextOffset) {
  outLines.clear();
  outLines.reserve(linesPerPage);
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  const size_t chunkSize = std::min(PAGE_BUFFER_SIZE - 1, fileSize - offset);
  std::unique_ptr<uint8_t[]> temporaryBuffer;
  if (!pageBuffer) temporaryBuffer = makeUniqueNoThrow<uint8_t[]>(PAGE_BUFFER_SIZE);
  uint8_t* const buffer = pageBuffer ? pageBuffer.get() : temporaryBuffer.get();
  if (!buffer) {
    LOG_ERR("TRS", "OOM: %u-byte TXT page buffer", static_cast<unsigned>(PAGE_BUFFER_SIZE));
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    return false;
  }
  buffer[chunkSize] = '\0';

  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x01);
  }

  // Parse lines from buffer
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    size_t lineContentLen = lineEnd - pos;
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);
    size_t lineBytePos = 0;

    do {
      if (line.empty()) {
        outLines.emplace_back();
        break;
      }

      int lineWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR);

      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLen;
        line.clear();
        break;
      }

      // Find break point
      size_t breakPos = line.length();
      while (breakPos > 0 && renderer.getTextAdvanceX(cachedFontId, line.substr(0, breakPos).c_str(),
                                                      EpdFontFamily::REGULAR) > viewportWidth) {
        // Try to break at space
        size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          // Break at character boundary for UTF-8
          breakPos--;
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) {
            breakPos--;
          }
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      outLines.push_back(line.substr(0, breakPos));

      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    } while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage);

    if (line.empty()) {
      pos = lineEnd + 1;
    } else {
      pos = pos + lineBytePos;
      break;
    }
  }

  if (pos == 0 && !outLines.empty()) {
    pos = 1;
  }

  nextOffset = offset + pos;
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  return !outLines.empty();
}

uint32_t TxtReaderActivity::knownPageCount() const {
  return fallbackIndex ? static_cast<uint32_t>(pageOffsets.size()) : pageIndex.knownPageCount();
}

bool TxtReaderActivity::indexComplete() const { return fallbackIndex || pageIndex.isComplete(); }

bool TxtReaderActivity::readPageOffset(const uint32_t page, uint32_t& offset) {
  if (!fallbackIndex) return pageIndex.pageOffset(page, offset);
  if (page >= pageOffsets.size() || pageOffsets[page] > UINT32_MAX) return false;
  offset = static_cast<uint32_t>(pageOffsets[page]);
  return true;
}

void TxtReaderActivity::buildIndexSlice(GfxRenderer& renderer) {
  const auto resolvePendingPage = [this]() {
    if (!waitingForIndex || !indexingPopupShown) return false;
    const uint32_t availablePages = knownPageCount();
    if (pendingPage < availablePages) {
      currentPage = static_cast<int>(pendingPage);
    } else if (indexComplete()) {
      currentPage = static_cast<int>(availablePages);
    } else {
      return false;
    }
    waitingForIndex = false;
    indexingPopupShown = false;
    requestUpdate();
    return true;
  };

  if (resolvePendingPage()) return;
  if (indexComplete()) return;

  const uint32_t sliceStartedMs = millis();
  do {
    size_t nextOffset = nextIndexOffset;
    if (!loadPageAtOffset(renderer, nextIndexOffset, currentPageLines, nextOffset) || nextOffset <= nextIndexOffset) {
      LOG_ERR("TRS", "Incremental TXT pagination failed at byte %u", static_cast<unsigned>(nextIndexOffset));
      fallbackIndex = true;
      useBlockingFallback(renderer);
      requestUpdate();
      return;
    }

    nextIndexOffset = static_cast<uint32_t>(nextOffset);
    if (nextOffset >= txt->getFileSize()) {
      const uint32_t sliceMs = millis() - sliceStartedMs;
      if (sliceMs > maxSliceMs) maxSliceMs = sliceMs;
      if (!pageIndex.finish()) {
        fallbackIndex = true;
        useBlockingFallback(renderer);
      } else {
        totalPages = static_cast<int>(pageIndex.knownPageCount());
        const uint32_t elapsed = millis() - indexStartedMs;
        LOG_DBG("TRS", "TXT index complete: %u pages, %lu ms, max slice %lu ms, heap %u, max block %u",
                static_cast<unsigned>(pageIndex.knownPageCount()), static_cast<unsigned long>(elapsed),
                static_cast<unsigned long>(maxSliceMs), static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
      }
      resolvePendingPage();
      requestUpdate();
      return;
    }

    if (!pageIndex.appendPageOffset(nextIndexOffset)) {
      LOG_ERR("TRS", "Failed to append incremental TXT page offset");
      fallbackIndex = true;
      useBlockingFallback(renderer);
      requestUpdate();
      return;
    }

    const uint32_t pages = pageIndex.knownPageCount();
    if (pages - lastLoggedPageCount >= INDEX_LOG_INTERVAL) {
      lastLoggedPageCount = pages;
      LOG_DBG("TRS", "TXT index: %u pages, heap %u, max block %u", static_cast<unsigned>(pages),
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    }
  } while (millis() - sliceStartedMs < INDEX_SLICE_BUDGET_MS);

  const uint32_t sliceMs = millis() - sliceStartedMs;
  if (sliceMs > maxSliceMs) {
    maxSliceMs = sliceMs;
    LOG_DBG("TRS", "TXT index max slice: %lu ms", static_cast<unsigned long>(maxSliceMs));
  }
  resolvePendingPage();
}

void TxtReaderActivity::loop() {
  const bool inputPending = mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() ||
                            mappedInput.wasScreenTouchReleased() ||
                            mappedInput.homeButtonAction() != HomeButtonAction::Ignore;
  ReaderActivity::loop();

  if (!initialized || (indexComplete() && !waitingForIndex) || inputPending || RenderLock::peek()) return;
  RenderLock lock;
  buildIndexSlice(renderer);
}

void TxtReaderActivity::renderBook() {
  if (!txt) {
    return;
  }

  if (!initialized) {
    initializeReader(renderer);
  }

  if (waitingForIndex) {
    indexingPopupShown = true;
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    return;
  }

  const uint32_t availablePages = knownPageCount();
  if (availablePages == 0) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= static_cast<int>(availablePages)) currentPage = static_cast<int>(availablePages) - 1;

  // Load current page content
  uint32_t offset = 0;
  if (!readPageOffset(static_cast<uint32_t>(currentPage), offset)) {
    LOG_ERR("TRS", "Failed to load offset for TXT page %d", currentPage);
    return;
  }
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(renderer, offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage(renderer);

  // Save progress
  saveProgress();

  if (!firstPageLogged) {
    firstPageLogged = true;
    LOG_DBG("TRS", "TXT first page: %lu ms, indexed pages %u, heap %u, max block %u",
            static_cast<unsigned long>(millis() - openStartedMs), static_cast<unsigned>(availablePages),
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
}

void TxtReaderActivity::renderPage(GfxRenderer& renderer) {
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& line : currentPageLines) {
      if (!line.empty()) {
        int x = cachedOrientedMarginLeft;
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        const int textWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR);

        // Apply text alignment
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            x = cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            break;
        }

        renderer.drawText(cachedFontId, x, y, line.c_str());
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();      // scan pass
  renderStatusBar();  // scan: a CJK title joins the batch prewarm
  scope.endScanAndPrewarm();

  // BW rendering
  renderLines();
  renderStatusBar();

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
}

void TxtReaderActivity::renderStatusBar() const {
  const bool complete = indexComplete();
  const int exactPageCount = complete ? static_cast<int>(knownPageCount()) : 0;
  const float progress = exactPageCount > 0 ? (currentPage + 1) * 100.0f / exactPageCount : 0;
  std::string title;
  if (SETTINGS.statusBarSpec().showsTitle()) {
    title = txt->getTitle();
  }
  GUI.drawStatusBar(renderer, progress, currentPage + 1, exactPageCount, title);
}

bool TxtReaderActivity::pageTurn(bool isForward) {
  // Ignore paging until initializeReader has established the page index
  if (!initialized) {
    return false;
  }
  if (isForward) {
    const uint32_t availablePages = knownPageCount();
    if (currentPage + 1 < static_cast<int>(availablePages)) {
      currentPage++;
      return true;
    }
    if (indexComplete()) {
      if (currentPage < static_cast<int>(availablePages)) {
        currentPage++;
        return true;
      }
      return false;
    }
    pendingPage = static_cast<uint32_t>(currentPage + 1);
    waitingForIndex = true;
    indexingPopupShown = false;
    return true;
  } else {
    if (waitingForIndex) {
      waitingForIndex = false;
      indexingPopupShown = false;
    }
    if (currentPage > 0) {
      currentPage--;
      return true;
    }
  }
  return false;
}

bool TxtReaderActivity::skipPages(int amount) {
  if (!initialized) {
    return false;
  }
  if (amount < 0) {
    waitingForIndex = false;
    indexingPopupShown = false;
  }
  int newPage = currentPage + amount;
  if (newPage < 0) newPage = 0;
  const int availablePages = static_cast<int>(knownPageCount());
  if (!indexComplete() && newPage >= availablePages) {
    pendingPage = static_cast<uint32_t>(newPage);
    waitingForIndex = true;
    indexingPopupShown = false;
    return true;
  }
  if (newPage > availablePages) newPage = availablePages;
  if (newPage != currentPage) {
    currentPage = newPage;
    return true;
  }
  return false;
}

bool TxtReaderActivity::isAtEndOfBook() const {
  return initialized && indexComplete() && currentPage >= static_cast<int>(knownPageCount());
}

void TxtReaderActivity::onReturnFromEndOfBook() {
  const uint32_t pages = knownPageCount();
  currentPage = pages > 0 ? static_cast<int>(pages) - 1 : 0;
}

void TxtReaderActivity::saveProgress() const {
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = 0;
  data[3] = 0;
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data))) {
    LOG_ERR("TRS", "Failed to save progress: page %d", currentPage);
  }
}

void TxtReaderActivity::loadProgress() {
  HalFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
      const int pages = static_cast<int>(knownPageCount());
      if (currentPage >= pages) {
        currentPage = pages - 1;
      }
      if (currentPage < 0) {
        currentPage = 0;
      }
      LOG_DBG("TRS", "Loaded progress: page %d/%d", currentPage, pages);
    }
  }
}

bool TxtReaderActivity::loadPageIndexCache() {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    return false;
  }

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  uint32_t numPages;
  serialization::readPod(f, numPages);

  pageOffsets.clear();
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void TxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = indexComplete() ? static_cast<int>(knownPageCount()) : 0;
  info.progressPercent =
      info.totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / info.totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
