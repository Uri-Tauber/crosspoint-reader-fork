#include "MarkdownReaderActivity.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long goHomeMs = 1000;
}

void MarkdownReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!markdown) {
    return;
  }

  // Configure screen orientation based on settings
  switch (SETTINGS.orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }

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
  ActivityWithSubactivity::onExit();

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  section.reset();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  markdown.reset();
}

void MarkdownReaderActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (pendingGoHome) {
    pendingGoHome = false;
    onGoHome();
    return;
  }

  // Long press BACK goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    onGoBack();
    return;
  }

  // Short press BACK goes to home
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    onGoHome();
    return;
  }

  // When long-press chapter skip is disabled, turn pages on press instead of release.
  const bool usePressForPageTurn = !SETTINGS.longPressChapterSkip;
  const bool prevTriggered = usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasPressed(MappedInputManager::Button::Left))
                                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasReleased(MappedInputManager::Button::Left));
  const bool powerPageTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered = usePressForPageTurn
                                 ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasPressed(MappedInputManager::Button::Right))
                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasReleased(MappedInputManager::Button::Right));

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

void MarkdownReaderActivity::render(Activity::RenderLock&&) {
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

  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  // Grayscale rendering (anti-aliased text)
  if (SETTINGS.textAntiAliasing) {
    renderer.storeBwBuffer();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);

    renderer.restoreBwBuffer();
  }
}

void MarkdownReaderActivity::renderStatusBar() const {
  if (!section) return;

  const float progress = section->pageCount > 0
                             ? static_cast<float>(section->currentPage + 1) * 100.0f / section->pageCount
                             : 0;
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
