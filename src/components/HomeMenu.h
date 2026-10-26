#pragma once

#include <I18n.h>

#include "components/themes/BaseTheme.h"
#include "util/HomeMenuSelection.h"

class GfxRenderer;

namespace home_menu {

Mask available();
StrId label(Item item);
UIIcon icon(Item item);
// Updates preferences only; the caller saves alongside its settings change.
bool refreshAvailability();
void reconcile(int capacity);
Mask takeHidden();

}  // namespace home_menu
