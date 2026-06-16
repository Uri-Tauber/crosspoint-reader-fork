#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

class OptionPopup {
 public:
  void show(StrId titleId, const StrId* optionIds, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    strIds = optionIds;
    strIdCount = optionCount;
    rawStrings = nullptr;
    ownedStrings.clear();
    selectedIndex = currentIndex;
    onSelectCallback = std::move(onSelect);
    active = true;
  }

  void show(const char* titleStr, const char* const* options, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = titleStr;
    rawStrings = options;
    strIdCount = optionCount;
    strIds = nullptr;
    ownedStrings.clear();
    selectedIndex = currentIndex;
    onSelectCallback = std::move(onSelect);
    active = true;
  }

  void show(StrId titleId, const std::vector<std::string>& options, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    ownedStrings = options;
    strIdCount = static_cast<int>(options.size());
    strIds = nullptr;
    rawStrings = nullptr;
    selectedIndex = currentIndex;
    onSelectCallback = std::move(onSelect);
    active = true;
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    if (input.wasPressed(MappedInputManager::Button::Up) || input.wasPressed(MappedInputManager::Button::Left)) {
      selectedIndex = (selectedIndex - 1 + strIdCount) % strIdCount;
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Down) ||
               input.wasPressed(MappedInputManager::Button::Right)) {
      selectedIndex = (selectedIndex + 1) % strIdCount;
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

  void render(GfxRenderer& renderer) const {
    if (!active) return;
    if (!ownedStrings.empty()) {
      GUI.drawOptionPopup(renderer, title, ownedStrings, selectedIndex);
      return;
    }
    std::vector<std::string> opts;
    opts.reserve(strIdCount);
    for (int i = 0; i < strIdCount; i++) {
      opts.emplace_back(strIds ? I18N.get(strIds[i]) : rawStrings[i]);
    }
    GUI.drawOptionPopup(renderer, title, opts, selectedIndex);
  }

  bool isActive() const { return active; }

 private:
  bool active = false;
  const char* title = nullptr;
  const StrId* strIds = nullptr;
  const char* const* rawStrings = nullptr;
  std::vector<std::string> ownedStrings;
  int strIdCount = 0;
  int selectedIndex = 0;
  std::function<void(int)> onSelectCallback;
};
