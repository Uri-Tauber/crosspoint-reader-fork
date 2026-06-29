#pragma once
#include <FreeInkUI.h>
#include <FreeInkUIGfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Popup-selection widget that wraps freeink::ui::contextMenu.
//
// History: this used to be a self-contained immediate-mode component
// (see commit 8d730daa0). The freeink-sdk ships a contextMenu that already
// covers the same job (panel + title + vertical option rows + selection state
// + style-driven theming), so we now route rendering through that. The
// public API is unchanged so the three activities that own this widget —
// EpubReaderMenuActivity, SettingsActivity, StatusBarSettingsActivity —
// still call show()/handleInput()/processRender()/render()/isActive() exactly
// as before.
//
// Per-theme visuals (border, padding, font weight, light vs dark selection,
// title separator, item row radius) are expressed by the existing
// ThemeMetrics::optionPopup* values, which are read directly from
// UITheme::getInstance().getMetrics() and translated into ContextMenuProps /
// StyleSet fields below.
class OptionPopup {
 public:
  void show(StrId titleId, const StrId* optionIds, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = I18N.get(optionIds[i]);
    }
    selectedIndex = currentIndex;
    onSelectCallback = std::move(onSelect);
    active = true;
  }

  void show(const char* titleStr, const char* const* options, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = titleStr;
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = options[i];
    }
    selectedIndex = currentIndex;
    onSelectCallback = std::move(onSelect);
    active = true;
  }

  void show(StrId titleId, const std::vector<std::string>& options, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    ownedStrings = options;
    selectedIndex = currentIndex;
    onSelectCallback = std::move(onSelect);
    active = true;
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    const int count = static_cast<int>(ownedStrings.size());
    if (input.wasPressed(MappedInputManager::Button::Up) || input.wasPressed(MappedInputManager::Button::Left)) {
      selectedIndex = (selectedIndex - 1 + count) % count;
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Down) ||
               input.wasPressed(MappedInputManager::Button::Right)) {
      selectedIndex = (selectedIndex + 1) % count;
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Confirm)) {
      active = false;
      if (onSelectCallback) onSelectCallback(selectedIndex);
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Back)) {
      active = false;
      requestUpdate();
      return true;
    }
    return true;
  }

  bool processRender(GfxRenderer& renderer, const MappedInputManager& input) {
    if (!active) return false;
    const auto popupLabels = input.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, popupLabels.btn1, popupLabels.btn2, popupLabels.btn3, popupLabels.btn4);
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

  void render(GfxRenderer& renderer) {
    if (!active || ownedStrings.empty()) return;

    namespace fui = freeink::ui;

    const auto& metrics = UITheme::getInstance().getMetrics();

    // Font slot bindings for the SDK target. The title always uses UI_12
    // bold; items use the theme's optionPopupUseSmallFont / optionPopupOptionFontBold
    // choice. FONT_SMALL is unused but bound so the slot is never queried.
    const int optionFontId = metrics.optionPopupUseSmallFont ? UI_10_FONT_ID : UI_12_FONT_ID;
    const EpdFontFamily::Style optionStyle =
        metrics.optionPopupOptionFontBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int titleFontId = UI_12_FONT_ID;
    const EpdFontFamily::Style titleStyle = EpdFontFamily::BOLD;

    // Dialog dimensions — verbatim from the legacy drawOptionPopup.
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    const int titleLineHeight = renderer.getLineHeight(titleFontId);
    const int optionLineHeight = renderer.getLineHeight(optionFontId);
    const int rowHeight = optionLineHeight + metrics.optionPopupSelectionVPadding * 2;
    const int itemSpacing = metrics.optionPopupItemSpacing;
    const int innerPadding = metrics.optionPopupInnerPadding;
    const int titleGap = metrics.optionPopupTitleGap;

    int maxTextWidth = renderer.getTextWidth(titleFontId, title.c_str(), titleStyle);
    for (const auto& opt : ownedStrings) {
      const int w = renderer.getTextWidth(optionFontId, opt.c_str(), optionStyle);
      if (w > maxTextWidth) maxTextWidth = w;
    }

    const int optionCount = static_cast<int>(ownedStrings.size());
    const int listHeight = rowHeight * optionCount + itemSpacing * (optionCount - 1);
    const int dialogW = std::min((maxTextWidth + innerPadding * 2 + metrics.optionPopupSelectionHPadding * 2) * 12 / 10,
                                 pageWidth - metrics.optionPopupDialogSideMargin * 2);
    const int contentHeight = titleLineHeight + titleGap + listHeight;
    const int dialogH = contentHeight + innerPadding * 2;
    const int dialogX = (pageWidth - dialogW) / 2;
    const int dialogY = (pageHeight - dialogH) / 2;

    // The legacy drawOptionPopup paints a white "halo" around the panel
    // (outer rect, frameRadius + frameThickness corner) before the black
    // border, so the panel boundary clears any chrome from the previous
    // screen without affecting the page background. We replicate that
    // here so the page bg matches before contextMenu draws the panel.
    const int frameThickness = metrics.popupFrameThickness;
    const int frameRadius = metrics.popupCornerRadius;
    if (frameRadius > 0) {
      renderer.fillRoundedRect(dialogX - frameThickness, dialogY - frameThickness, dialogW + frameThickness * 2,
                               dialogH + frameThickness * 2, frameRadius + frameThickness, Color::White);
    } else if (frameThickness > 0) {
      renderer.fillRect(dialogX - frameThickness, dialogY - frameThickness, dialogW + frameThickness * 2,
                        dialogH + frameThickness * 2, true);
    }

    // Build the DialogOption array. Strings live in ownedStrings so the
    // borrowed label pointers stay valid for the duration of contextMenu().
    std::vector<fui::DialogOption> dialogOptions;
    dialogOptions.resize(static_cast<size_t>(optionCount));
    for (int i = 0; i < optionCount; i++) {
      dialogOptions[i].label = ownedStrings[i].c_str();
      dialogOptions[i].action = static_cast<fui::ActionId>(i + 1);  // any non-NO_ACTION
      dialogOptions[i].value = static_cast<int16_t>(i);
      dialogOptions[i].state = (i == selectedIndex) ? fui::StateSelected : fui::StateNormal;
      dialogOptions[i].enabled = true;
    }

    fui::GfxRendererFrame<32> f(renderer, UI_12_FONT_ID, optionFontId, titleFontId);

    // Panel styles: white fill (matches legacy inner panel) plus a black
    // border with the theme's frame thickness and corner radius. The SDK's
    // stroke draws the border INSIDE the rect (GfxRenderer::drawRect /
    // drawRoundedRect inset the stroke), so the visible border occupies
    // frameThickness pixels starting at the rect edge inward — same as the
    // legacy double-rect (middle black + inner white) approach.
    fui::StyleSet panelStyles;
    panelStyles.explicitlySet = true;
    panelStyles.normal.background = fui::Paint::solid(fui::Color::White);
    panelStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
    panelStyles.normal.border = fui::Paint::solid(fui::Color::Black);
    panelStyles.normal.borderWidth = frameThickness > 0 ? static_cast<uint8_t>(frameThickness) : uint8_t{0};
    panelStyles.normal.radius = frameRadius > 0 ? static_cast<uint8_t>(frameRadius) : uint8_t{0};

    // Item styles. Unselected rows have no background fill so the panel
    // white shows through; the text comes from normal.foreground. Selected
    // rows get either a solid black (dark selection, white text) or a
    // light-gray dither (light selection, dark text), matching the legacy
    // behavior controlled by optionPopupSelectionLight. The rounded item
    // radius is the theme's optionPopupSelectionRadius.
    fui::StyleSet itemStyles;
    itemStyles.explicitlySet = true;
    itemStyles.normal.background = fui::Paint::none();
    itemStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
    itemStyles.selected.background = metrics.optionPopupSelectionLight ? fui::Paint::dither(fui::Color::LightGray)
                                                                       : fui::Paint::solid(fui::Color::Black);
    itemStyles.selected.foreground =
        metrics.optionPopupSelectionLight ? fui::Paint::solid(fui::Color::Black) : fui::Paint::solid(fui::Color::White);
    itemStyles.focused = itemStyles.selected;
    itemStyles.active = itemStyles.selected;
    itemStyles.disabled.background = fui::Paint::none();
    itemStyles.disabled.foreground = fui::Paint::dither(fui::Color::LightGray);
    if (metrics.optionPopupSelectionRadius > 0) {
      const uint8_t r = static_cast<uint8_t>(metrics.optionPopupSelectionRadius);
      itemStyles.normal.radius = r;
      itemStyles.selected.radius = r;
      itemStyles.focused.radius = r;
      itemStyles.active.radius = r;
      itemStyles.disabled.radius = r;
    }

    fui::TextStyle titleText;
    titleText.font = fui::GfxRendererTarget::FONT_TITLE;
    titleText.align = fui::TextAlign::Center;
    titleText.bold = true;
    titleText.color = fui::Color::Black;

    fui::TextStyle itemText;
    itemText.font = fui::GfxRendererTarget::FONT_BODY;
    itemText.align = fui::TextAlign::Center;
    itemText.bold = (optionStyle == EpdFontFamily::BOLD);
    itemText.color = fui::Color::Black;

    fui::ContextMenuProps props;
    props.title = title.c_str();
    props.options = dialogOptions.data();
    props.optionCount = static_cast<uint8_t>(optionCount);
    props.titleText = titleText;
    props.itemText = itemText;
    props.panelStyles = panelStyles;
    props.itemStyles = itemStyles;
    props.padding = fui::Insets{static_cast<int16_t>(innerPadding), static_cast<int16_t>(innerPadding),
                                static_cast<int16_t>(innerPadding), static_cast<int16_t>(innerPadding)};
    props.rowHeight = static_cast<int16_t>(rowHeight);
    props.gap = static_cast<int16_t>(itemSpacing);
    // Activity owns input — interactions are visual only. Routing through
    // frame.finish() is irrelevant because the InputSnapshot we hand the
    // frame is empty.
    props.inputMask = fui::InputNone;
    props.dimBackground = false;
    props.titleSeparator = metrics.optionPopupTitleSeparator;

    const fui::Rect panelRect = {static_cast<int16_t>(dialogX), static_cast<int16_t>(dialogY),
                                 static_cast<int16_t>(dialogW), static_cast<int16_t>(dialogH)};
    fui::contextMenu(f.frame, panelRect, props);
  }

  bool isActive() const { return active; }

 private:
  bool active = false;
  std::string title;
  std::vector<std::string> ownedStrings;
  int selectedIndex = 0;
  std::function<void(int)> onSelectCallback;
};