#pragma once

#include <HalStorage.h>

#include <cstddef>

#include "LibraryFormat.h"

namespace library {
struct BufferedSortKey {
  char text[SORT_TEXT_BYTES]{};
  SeriesPositionKey series;
  uint32_t date = 0;
};
struct SortWorkspace {
  alignas(std::max_align_t) uint8_t bytes[6144];
};
static_assert(sizeof(SortWorkspace) == 6144);
using LoadSortKey = bool (*)(void*, uint16_t, uint8_t, void*);
using CompareSortKeys = int (*)(const void*, const void*, uint8_t);
bool bufferedSort(uint16_t count, uint8_t field, size_t keySize, LoadSortKey load, CompareSortKeys compare,
                  void* context, SortWorkspace& workspace, uint16_t* order, uint16_t& passes);
int compareMetadataSortKeys(const void* a, const void* b, uint8_t field);
}  // namespace library
