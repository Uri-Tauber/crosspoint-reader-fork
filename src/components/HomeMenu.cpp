#include "HomeMenu.h"

#include "CrossPointSettings.h"
#include "OpdsServerStore.h"

namespace home_menu {
namespace {
Mask pendingHidden = 0;
}

Mask available() { return availableItems(SETTINGS.pluginsEnabled != 0, OPDS_STORE.hasServers()); }

StrId label(Item item) {
  switch (item) {
    case Item::Files:
      return StrId::STR_BROWSE_FILES;
    case Item::Library:
      return StrId::STR_LIBRARY;
    case Item::Opds:
      return StrId::STR_OPDS_BROWSER;
    case Item::Plugins:
      return StrId::STR_PLUGINS;
    case Item::Transfer:
      return StrId::STR_FILE_TRANSFER;
    case Item::Settings:
      return StrId::STR_SETTINGS_TITLE;
  }
  return StrId::STR_HOME_MENU;
}

UIIcon icon(Item item) {
  switch (item) {
    case Item::Files:
      return Folder;
    case Item::Library:
      return Library;
    case Item::Opds:
    case Item::Plugins:
      return Plugins;
    case Item::Transfer:
      return Transfer;
    case Item::Settings:
      return Settings;
  }
  return Settings;
}

bool refreshAvailability() {
  return updateAvailability(SETTINGS.homeMenuItems, SETTINGS.homeMenuAvailability, available());
}

void reconcile(int capacity) {
  const bool changed = refreshAvailability();
  const Mask removed = trim(SETTINGS.homeMenuItems, available(), capacity);
  pendingHidden |= removed;
  if (changed || removed) SETTINGS.saveToFile();
}

Mask takeHidden() {
  const Mask hidden = pendingHidden & ~SETTINGS.homeMenuItems & available();
  pendingHidden = 0;
  return hidden;
}

}  // namespace home_menu
