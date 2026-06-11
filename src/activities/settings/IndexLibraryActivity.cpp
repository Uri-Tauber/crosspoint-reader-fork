#include "IndexLibraryActivity.h"

#include <Epub.h>
#include <Epub/Section.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"

static const char* TAG = "IndexLib";

void IndexLibraryActivity::onEnter() {
  Activity::onEnter();
  state = WARNING;
  requestUpdate();
}

void IndexLibraryActivity::onExit() { Activity::onExit(); }

void IndexLibraryActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_INDEX_LIBRARY));

  if (state == WARNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 60, tr(STR_INDEX_LIBRARY_WARNING_1), true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 30, tr(STR_INDEX_LIBRARY_WARNING_2), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_INDEX_LIBRARY_WARNING_3), true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, tr(STR_INDEX_LIBRARY_WARNING_4), true);

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == INDEXING) {
    char progressBuf[128];
    snprintf(progressBuf, sizeof(progressBuf), "%d / %d", processedBooks + 1, totalBooks);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 30, tr(STR_INDEXING_LIBRARY));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, progressBuf);
    if (!currentBookName.empty()) {
      // Truncate long names to fit the screen
      std::string displayName = currentBookName;
      if (displayName.length() > 40) {
        displayName = displayName.substr(0, 37) + "...";
      }
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, displayName.c_str());
    }
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 30, tr(STR_LIBRARY_INDEXED), true, EpdFontFamily::BOLD);

    std::string resultText = std::to_string(indexedChapters) + " " + std::string(tr(STR_CHAPTERS_INDEXED));
    if (skippedChapters > 0) {
      resultText += ", " + std::to_string(skippedChapters) + " " + std::string(tr(STR_ALREADY_CACHED));
    }
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, resultText.c_str());

    if (failedChapters > 0) {
      std::string failText = std::to_string(failedChapters) + " " + std::string(tr(STR_FAILED_LOWER));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, failText.c_str());
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_INDEX_LIBRARY_FAILED), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_CHECK_SERIAL_OUTPUT));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void IndexLibraryActivity::computeViewport(uint16_t& outWidth, uint16_t& outHeight) {
  // Mirror the viewport computation from EpubReaderActivity::render() so that
  // the section caches we build are valid when the reader loads them later.
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;
  orientedMarginBottom +=
      std::max(static_cast<int>(SETTINGS.screenMargin), static_cast<int>(UITheme::getInstance().getStatusBarHeight()));

  outWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  outHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
}

void IndexLibraryActivity::findEpubFiles(const char* rootPath, std::vector<std::string>& outPaths) {
  // Iterative DFS to find all .epub files, skipping hidden dirs and system folders.
  std::vector<std::string> dirStack;
  dirStack.push_back(rootPath);

  char nameBuf[256];

  while (!dirStack.empty()) {
    std::string currentDir = std::move(dirStack.back());
    dirStack.pop_back();

    auto dir = Storage.open(currentDir.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }

    dir.rewindDirectory();
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(nameBuf, sizeof(nameBuf));

      // Skip hidden files/dirs and system dirs (same filter as FileBrowserActivity)
      if (nameBuf[0] == '.' || strcmp(nameBuf, "System Volume Information") == 0) {
        file.close();
        continue;
      }

      std::string fullPath = currentDir;
      if (fullPath.back() != '/') fullPath += "/";
      fullPath += nameBuf;

      const bool isDir = file.isDirectory();
      file.close();

      if (isDir) {
        dirStack.push_back(std::move(fullPath));
      } else if (FsHelpers::hasEpubExtension(std::string_view(nameBuf))) {
        outPaths.push_back(std::move(fullPath));
      }
    }
    dir.close();
  }
}

void IndexLibraryActivity::indexLibrary() {
  LOG_DBG(TAG, "Starting library indexing...");

  // Ensure SD card fonts are loaded (mirrors ReaderActivity::onEnter)
  sdFontSystem.ensureLoaded(renderer);

  // Phase 1: discover all EPUB files on the SD card
  std::vector<std::string> epubPaths;
  findEpubFiles("/", epubPaths);

  totalBooks = static_cast<int>(epubPaths.size());
  if (totalBooks == 0) {
    LOG_DBG(TAG, "No EPUB files found on SD card");
    state = SUCCESS;
    requestUpdate();
    return;
  }

  LOG_DBG(TAG, "Found %d EPUB files", totalBooks);

  // Capture all reader settings once — they stay constant across the entire run
  uint16_t viewportWidth, viewportHeight;
  computeViewport(viewportWidth, viewportHeight);

  const int fontId = SETTINGS.getReaderFontId();
  const float lineCompression = SETTINGS.getReaderLineCompression();
  const bool extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  const uint8_t paragraphAlignment = SETTINGS.paragraphAlignment;
  const bool hyphenationEnabled = SETTINGS.hyphenationEnabled;
  const bool embeddedStyle = SETTINGS.embeddedStyle;
  const uint8_t imageRendering = SETTINGS.imageRendering;
  const bool focusReadingEnabled = SETTINGS.focusReadingEnabled;

  // Phase 2: index each book
  for (int i = 0; i < totalBooks; i++) {
    processedBooks = i;

    // Extract filename for display (strip path + extension)
    const auto& path = epubPaths[i];
    const auto lastSlash = path.find_last_of('/');
    currentBookName = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
    const auto dotPos = currentBookName.rfind('.');
    if (dotPos != std::string::npos) {
      currentBookName = currentBookName.substr(0, dotPos);
    }

    // Update screen once per book
    requestUpdateAndWait();

    // Open the EPUB (builds book.bin cache on first open)
    auto epub = std::make_shared<Epub>(path, "/.crosspoint");
    if (!epub->load(true, embeddedStyle == 0)) {
      LOG_ERR(TAG, "Failed to load epub: %s", path.c_str());
      continue;
    }

    const int spineCount = epub->getSpineItemsCount();
    LOG_DBG(TAG, "Indexing %s (%d spine items)", path.c_str(), spineCount);

    for (int s = 0; s < spineCount; s++) {
      Section section(epub, s, renderer);

      // Check if this spine item is already cached with the current settings
      if (section.loadSectionFile(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                                  viewportHeight, hyphenationEnabled, embeddedStyle, imageRendering,
                                  focusReadingEnabled)) {
        skippedChapters++;
        continue;
      }

      // Build the section cache (this is the heavy operation)
      if (section.createSectionFile(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                                    viewportHeight, hyphenationEnabled, embeddedStyle, imageRendering,
                                    focusReadingEnabled)) {
        indexedChapters++;
      } else {
        LOG_ERR(TAG, "Failed to index spine %d of %s", s, path.c_str());
        failedChapters++;
      }
    }
  }

  processedBooks = totalBooks;
  LOG_DBG(TAG, "Library indexing complete: %d indexed, %d cached, %d failed", indexedChapters, skippedChapters,
          failedChapters);

  state = SUCCESS;
  requestUpdate();
}

void IndexLibraryActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      LOG_DBG(TAG, "User confirmed, starting library indexing");
      {
        RenderLock lock(*this);
        state = INDEXING;
      }
      requestUpdateAndWait();

      indexLibrary();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      LOG_DBG(TAG, "User cancelled");
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}
