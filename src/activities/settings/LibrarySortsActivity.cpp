#include "LibrarySortsActivity.h"

#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "activities/library/LibrarySortLabels.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void LibrarySortsActivity::onEnter() {
  workingMask = library::sanitizeSorts(SETTINGS.librarySorts);
  for (uint8_t i = 0; i < library::SORT_COUNT; ++i) {
    rowItems[i].label = librarySortLabel(static_cast<library::SortKind>(i));
    rowItems[i].actionValue = i;
  }
  UiListActivity::onEnter();
}
void LibrarySortsActivity::onExit() {
  if (edited && workingMask != SETTINGS.librarySorts) {
    SETTINGS.librarySorts = workingMask;
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}
const char* LibrarySortsActivity::headerTitle() const { return tr(STR_LIBRARY_SORTS); }

void LibrarySortsActivity::activateIndex(int index) {
  if (index < 0 || index >= library::SORT_COUNT) return;
  nav.selected = index;
  app.clearTapFlash();
  const uint16_t bit = 1u << index;
  const auto count = library::enabledSortCount(workingMask);
  if ((workingMask & bit) ? count > 1 : count < library::MAX_ENABLED_SORTS) {
    workingMask ^= bit;
    edited = true;
    SETTINGS.librarySorts = workingMask;
    SETTINGS.saveToFile();
  }
  requestUpdate();
}
void LibrarySortsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
                                      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const auto count = library::enabledSortCount(workingMask);
  for (uint8_t i = 0; i < library::SORT_COUNT; ++i) {
    const bool enabled = workingMask & (1u << i);
    rowItems[i].value = enabled
                            ? (count == 1 ? tr(STR_LIBRARY_SORT_REQUIRED) : tr(STR_STATE_ON))
                            : (count == library::MAX_ENABLED_SORTS ? tr(STR_LIBRARY_SORT_LIMIT) : tr(STR_STATE_OFF));
  }
  fui::ListProps props;
  props.items = rowItems;
  props.count = library::SORT_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
