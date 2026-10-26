#pragma once

#include "activities/UiListActivity.h"
#include "util/HomeMenuSelection.h"

class HomeMenuSettingsActivity final : public UiListActivity {
 public:
  HomeMenuSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("HomeMenuSettings", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return items.size; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

  home_menu::Items items;
  home_menu::Mask workingMask = 0;
  home_menu::Mask availableMask = 0;
  int capacity = 0;
  bool limitReached = false;
  freeink::ui::ListItem rows[home_menu::COUNT]{};
  char countText[64]{};
};
