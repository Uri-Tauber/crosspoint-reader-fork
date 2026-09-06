#pragma once

#include <LibrarySort.h>

#include "activities/UiListActivity.h"

class LibrarySortsActivity final : public UiListActivity {
 public:
  LibrarySortsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("LibrarySorts", renderer, input) {}
  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return library::SORT_COUNT; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  uint16_t workingMask = library::DEFAULT_SORTS;
  bool edited = false;
  freeink::ui::ListItem rowItems[library::SORT_COUNT]{};
};
