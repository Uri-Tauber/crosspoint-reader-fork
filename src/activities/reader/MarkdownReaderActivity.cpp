#include "MarkdownReaderActivity.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

void MarkdownReaderActivity::onEnter() {
  Activity::onEnter();

  if (!markdown) {
    return;
  }

  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  markdown->setupCacheDir();

  // Save current markdown as last opened file and add to recent books
  auto filePath = markdown->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "");

  requestUpdate();
}

void MarkdownReaderActivity::onExit() {
  Activity::onExit();

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  section.reset();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  markdown.reset();
}

void MarkdownReaderActivity::loop() {
  // Long press BACK goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    activityManager.goToFileBrowser(markdown ? markdown->getPath() : "");
    return;
  }

  // Short press BACK goes to home
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    onGoHome();
    return;
  }

  auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (!section) {
    requestUpdate();
    return;
  }

  if (prevTriggered && section->currentPage > 0) {
    section->currentPage--;
    requestUpdate();
  } else if (nextTriggered && section->currentPage < section->pageCount - 1) {
    section->currentPage++;
    requestUpdate();
  }
}

void MarkdownReaderActivity::render(RenderLock&&) {
  if (!markdown) {
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;
  orientedMarginBottom +=
      std::max(SETTINGS.screenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  if (!section) {
    LOG_DBG("MDR", "Loading markdown section");
    section = std::unique_ptr<MarkdownSection>(new MarkdownSection(markdown, renderer));

    const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

    if (!section->loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                  viewportHeight, SETTINGS.hyphenationEnabled)) {
      LOG_DBG("MDR", "Cache not found, building...");

      const auto popupFn = [this]() { GUI.drawPopup(renderer, tr(STR_INDEXING)); };

      if (!section->createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                      SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                      viewportHeight, SETTINGS.hyphenationEnabled, popupFn)) {
        LOG_ERR("MDR", "Failed to persist page data to SD");
        section.reset();
        renderer.clearScreen();
        renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_LOAD_MD_FAILED), true, EpdFontFamily::BOLD);
        renderer.displayBuffer();
        return;
      }
    } else {
      LOG_DBG("MDR", "Cache found, skipping build...");
    }

    // Restore saved progress
    loadProgress();
    if (nextPageNumber >= section->pageCount) {
      section->currentPage = section->pageCount > 0 ? section->pageCount - 1 : 0;
    } else {
      section->currentPage = nextPageNumber;
    }
  }

  // Show end of document screen
  if (section->pageCount == 0 || section->currentPage >= section->pageCount) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_END_OF_BOOK), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen();

  if (section->currentPage < 0) {
    section->currentPage = 0;
  }

  {
    auto p = section->loadPageFromSectionFile();
    if (!p) {
      LOG_ERR("MDR", "Failed to load page from SD - clearing section cache");
      section->clearCache();
      section.reset();
      requestUpdate();
      return;
    }

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("MDR", "Rendered page in %lums", millis() - start);
    renderer.clearFontCache();
  }

  saveProgress();
}

void MarkdownReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                            const int orientedMarginRight, const int orientedMarginBottom,
                                            const int orientedMarginLeft) {
  (void)orientedMarginRight;
  (void)orientedMarginBottom;

  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
  renderStatusBar();

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&page, this, orientedMarginLeft, orientedMarginTop]() {
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    });
  }
}

void MarkdownReaderActivity::renderStatusBar() const {
  if (!section) return;

  const float progress =
      section->pageCount > 0 ? static_cast<float>(section->currentPage + 1) * 100.0f / section->pageCount : 0;
  std::string title = markdown ? markdown->getTitle() : "";

  GUI.drawStatusBar(renderer, progress, section->currentPage + 1, section->pageCount, title);
}

void MarkdownReaderActivity::saveProgress() const {
  if (!markdown || !section) return;

  FsFile f;
  if (Storage.openFileForWrite("MDR", markdown->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = section->currentPage & 0xFF;
    data[1] = (section->currentPage >> 8) & 0xFF;
    data[2] = 0;
    data[3] = 0;
    f.write(data, 4);
    f.close();
  }
}

void MarkdownReaderActivity::loadProgress() {
  if (!markdown) return;

  FsFile f;
  if (Storage.openFileForRead("MDR", markdown->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      nextPageNumber = data[0] + (data[1] << 8);
      LOG_DBG("MDR", "Loaded progress: page %d", nextPageNumber);
    }
    f.close();
  }
}
