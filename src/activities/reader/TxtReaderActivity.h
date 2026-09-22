#pragma once

#include <Txt.h>
#include <TxtPageIndex.h>

#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReaderActivity.h"

class TxtReaderActivity final : public ReaderActivity {
  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;

  TxtPageIndex pageIndex;
  std::unique_ptr<uint8_t[]> pageBuffer;
  uint32_t nextIndexOffset = 0;
  uint32_t pendingPage = 0;
  uint32_t indexStartedMs = 0;
  uint32_t openStartedMs = 0;
  uint32_t maxSliceMs = 0;
  uint32_t lastLoggedPageCount = 0;
  bool fallbackIndex = false;
  bool waitingForIndex = false;
  bool indexingPopupShown = false;
  bool firstPageLogged = false;

  // Temporary blocking fallback for an allocation or incremental-index failure.
  std::vector<size_t> pageOffsets;
  std::vector<std::string> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;

  // Cached settings for cache validation
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage(GfxRenderer& renderer);
  void initializeReader(GfxRenderer& renderer);
  bool loadPageAtOffset(const GfxRenderer& renderer, size_t offset, std::vector<std::string>& outLines,
                        size_t& nextOffset);
  void buildPageIndexBlocking(GfxRenderer& renderer);
  void buildIndexSlice(GfxRenderer& renderer);
  void useBlockingFallback(GfxRenderer& renderer);
  uint32_t knownPageCount() const;
  bool indexComplete() const;
  bool readPageOffset(uint32_t page, uint32_t& offset);
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void loadProgress();
  void renderStatusBar() const;

  bool loadBook() override;
  std::string getBookTitle() const override { return txt ? txt->getTitle() : ""; }
  void renderBook() override;

 public:
  static constexpr size_t PAGE_BUFFER_SIZE = 8 * 1024;
  static constexpr uint32_t INDEX_SLICE_BUDGET_MS = 10;

  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("TxtReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~TxtReaderActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
