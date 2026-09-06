#pragma once

#include <cstdint>

namespace library {
// Persisted IDs: append new kinds without renumbering existing ones.
enum class SortKind : uint8_t { Added, Title, Author, Date, Publisher, Language, Series, Subject, Count };
inline constexpr uint8_t SORT_COUNT = static_cast<uint8_t>(SortKind::Count);
inline constexpr uint8_t MAX_ENABLED_SORTS = 4;
inline constexpr uint16_t DEFAULT_SORTS = 7;
inline constexpr uint8_t METADATA_SORT_COUNT = SORT_COUNT - 3;

enum class SortOrder : uint8_t {
  AddedAsc,
  AddedDesc,
  TitleAsc,
  TitleDesc,
  AuthorAsc,
  AuthorDesc,
  DateAsc,
  DateDesc,
  PublisherAsc,
  PublisherDesc,
  LanguageAsc,
  LanguageDesc,
  SeriesAsc,
  SeriesDesc,
  SubjectAsc,
  SubjectDesc,
};
constexpr SortKind sortKind(SortOrder order) { return static_cast<SortKind>(static_cast<uint8_t>(order) / 2); }
constexpr bool descending(SortOrder order) { return (static_cast<uint8_t>(order) & 1) != 0; }
constexpr SortOrder sortOrder(SortKind kind, bool desc) {
  return static_cast<SortOrder>(static_cast<uint8_t>(kind) * 2 + desc);
}
constexpr uint8_t enabledSortCount(uint16_t mask) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < SORT_COUNT; ++i)
    if (mask & (1u << i)) ++count;
  return count;
}
constexpr uint16_t sanitizeSorts(uint16_t mask) {
  uint16_t result = 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < SORT_COUNT && count < MAX_ENABLED_SORTS; ++i) {
    if (mask & (1u << i)) {
      result |= 1u << i;
      ++count;
    }
  }
  return result ? result : DEFAULT_SORTS;
}
constexpr SortKind enabledSortAt(uint16_t mask, int tab) {
  for (uint8_t i = 0; i < SORT_COUNT; ++i) {
    if ((mask & (1u << i)) && tab-- == 0) return static_cast<SortKind>(i);
  }
  return SortKind::Added;
}
struct SeriesPositionKey {
  double major = 0;
  double decimal = 0;
  char dotted[32]{};
  uint8_t valid = 0;
  uint8_t subdivision = 0;
  uint8_t calibre = 0;
};
static_assert(sizeof(SeriesPositionKey) == 56);
SeriesPositionKey seriesPositionKey(const char* value, bool calibre);
int compareSeriesKeys(const SeriesPositionKey& a, const SeriesPositionKey& b);
// Calibre positions are decimals; EPUB 3 positions are dot-separated integers.
int compareSeriesPosition(const char* a, bool calibreA, const char* b, bool calibreB);
// YYYY, YYYY-MM, and YYYY-MM-DD (optionally followed by an ISO time).
// Invalid or absent dates have key zero and are sorted as unknown.
uint32_t publicationDateKey(const char* value);
}  // namespace library
