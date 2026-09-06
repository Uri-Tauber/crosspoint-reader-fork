#pragma once

#include <HalStorage.h>

#include "LibraryFormat.h"

namespace library {
struct BufferedSortKey {
  char text[SORT_TEXT_BYTES]{};
  SeriesPositionKey series;
  uint32_t date = 0;
  uint16_t ordinal = 0;
};
struct SortWorkspace {
  BufferedSortKey keys[12];
  uint8_t padding[6144 - 12 * sizeof(BufferedSortKey)];
};
static_assert(sizeof(SortWorkspace) == 6144);
using LoadSortKey = bool (*)(void*, uint16_t, uint8_t, BufferedSortKey&);
bool bufferedSort(uint16_t count, uint8_t field, LoadSortKey load, void* context, SortWorkspace& workspace,
                  uint16_t* order, uint16_t& passes);
bool sortKeyBefore(const BufferedSortKey& a, const BufferedSortKey& b, uint8_t field);
}  // namespace library
