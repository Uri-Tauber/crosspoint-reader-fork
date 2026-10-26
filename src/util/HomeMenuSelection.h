#pragma once

#include <array>
#include <cstdint>

namespace home_menu {

// Persisted bit positions; display order is defined separately below.
enum class Item : uint8_t { Files = 0, Library = 1, Opds = 2, Plugins = 3, Transfer = 4, Settings = 5 };
using Mask = uint16_t;

constexpr Mask itemBit(Item item) { return Mask{1} << static_cast<uint8_t>(item); }
constexpr std::array ORDER = {Item::Files, Item::Library, Item::Opds, Item::Plugins, Item::Transfer, Item::Settings};
constexpr int COUNT = ORDER.size();
constexpr Mask PINNED = itemBit(Item::Settings);
constexpr Mask ALWAYS_AVAILABLE = itemBit(Item::Files) | itemBit(Item::Library) | itemBit(Item::Transfer) | PINNED;
// New actions must opt into these defaults explicitly.
constexpr Mask DEFAULT_ITEMS = itemBit(Item::Files) | itemBit(Item::Library) | itemBit(Item::Opds) |
                               itemBit(Item::Plugins) | itemBit(Item::Transfer) | itemBit(Item::Settings);
constexpr Mask ENABLE_ON_AVAILABILITY = itemBit(Item::Opds) | itemBit(Item::Plugins);
constexpr Mask ALL_ITEMS = [] {
  Mask mask = 0;
  for (auto item : ORDER) mask |= itemBit(item);
  return mask;
}();

constexpr Mask normalize(Mask selected) { return (selected & ALL_ITEMS) | PINNED; }

constexpr Mask availableItems(bool plugins, bool opds) {
  return ALWAYS_AVAILABLE | (plugins ? itemBit(Item::Plugins) : opds ? itemBit(Item::Opds) : 0);
}

constexpr int count(Mask items) {
  int result = 0;
  for (auto item : ORDER) {
    if (items & itemBit(item)) ++result;
  }
  return result;
}

inline bool updateAvailability(Mask& selected, Mask& previous, Mask available) {
  const Mask updated = normalize(selected | (available & ~previous & ENABLE_ON_AVAILABILITY));
  const bool changed = selected != updated || previous != available;
  selected = updated;
  previous = available;
  return changed;
}

inline Mask trim(Mask& selected, Mask available, int capacity) {
  selected = normalize(selected);
  int remaining = count(selected & available);
  Mask removed = 0;
  for (auto it = ORDER.rbegin(); it != ORDER.rend() && remaining > capacity; ++it) {
    const Mask candidate = itemBit(*it);
    if (candidate != PINNED && (selected & available & candidate)) {
      selected &= ~candidate;
      removed |= candidate;
      --remaining;
    }
  }
  return removed;
}

struct Items {
  std::array<Item, COUNT> values{};
  int size = 0;

  int indexOf(Item item) const {
    for (int i = 0; i < size; ++i) {
      if (values[i] == item) return i;
    }
    return 0;
  }
};

inline Items collect(Mask mask) {
  Items result;
  for (auto item : ORDER) {
    if (mask & itemBit(item)) result.values[result.size++] = item;
  }
  return result;
}

constexpr int slotsThatFit(int extent, int itemSize, int gap) {
  return extent < itemSize || itemSize <= 0 || gap < 0 ? 0 : 1 + (extent - itemSize) / (itemSize + gap);
}

}  // namespace home_menu
