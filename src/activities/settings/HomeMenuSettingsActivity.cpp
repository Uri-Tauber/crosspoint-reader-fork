#include "HomeMenuSettingsActivity.h"

#include <cstdio>

#include "CrossPointSettings.h"
#include "components/HomeMenu.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void HomeMenuSettingsActivity::onEnter() {
  capacity = UITheme::getInstance().getHomeMenuCapacity(renderer);
  home_menu::reconcile(capacity);
  workingMask = SETTINGS.homeMenuItems;
  availableMask = home_menu::available();
  items = home_menu::collect(availableMask);
  for (int i = 0; i < items.size; ++i) {
    rows[i].label = I18N.get(home_menu::label(items.values[i]));
    rows[i].actionValue = i;
  }
  UiListActivity::onEnter();
}

void HomeMenuSettingsActivity::onExit() {
  if (workingMask != SETTINGS.homeMenuItems) {
    SETTINGS.homeMenuItems = workingMask;
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}

const char* HomeMenuSettingsActivity::headerTitle() const { return tr(STR_HOME_MENU); }

void HomeMenuSettingsActivity::activateIndex(int index) {
  nav.selected = index;
  app.clearTapFlash();
  const auto item = items.values[index];
  if (item == home_menu::Item::Settings) return;
  const auto bit = home_menu::itemBit(item);
  limitReached = !(workingMask & bit) && home_menu::count(workingMask & availableMask) >= capacity;
  if (!limitReached) workingMask ^= bit;
  requestUpdate();
}

void HomeMenuSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  snprintf(countText, sizeof(countText), tr(STR_HOME_MENU_SELECTED), home_menu::count(workingMask & availableMask),
           capacity);
  const auto style = screen.theme().smallText;
  const auto lineHeight = screen.target().lineHeight(style.font);
  screen.target().text(screen.takeTop(lineHeight), countText, style);
  if (limitReached) {
    fui::TextAreaProps warning;
    warning.text = tr(STR_HOME_MENU_FULL);
    warning.style = style;
    warning.showCaret = false;
    screen.textArea(warning, lineHeight * 2);
  }
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < items.size; ++i) {
    GUI.setCheckboxRow(rows[i], (workingMask & home_menu::itemBit(items.values[i])) != 0);
    if (items.values[i] == home_menu::Item::Settings) rows[i].enabled = false;
  }
  fui::ListProps props;
  props.items = rows;
  props.count = items.size;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
