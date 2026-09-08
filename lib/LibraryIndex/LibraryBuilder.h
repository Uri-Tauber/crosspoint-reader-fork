#pragma once

// Builds the SD library index with bounded scratch and transactional installation.
// Fresh records reuse source metadata. Only selected extended orders are prepared.

#include <cstdint>
#include <string>

#include "LibraryFormat.h"

namespace library {

// Directory levels below the scan root that are walked. The measured corpus is
// two deep (genre/author/book); the cap exists because a corrupted FAT can
// contain a directory that contains itself — also measured on the same card —
// and an uncapped walk would never return.
inline constexpr int LIBRARY_MAX_DEPTH = 5;

// Duplicate identities remembered while one directory is enumerated. The
// fixed, fallible allocation is 8 KiB at this cap; unlike std::vector it cannot
// grow into abort() when a damaged or unusually flat directory is scanned.
inline constexpr uint16_t LIBRARY_MAX_DEDUP_KEYS = 1024;

struct BuildStats {
  uint16_t books = 0;
  uint16_t folders = 0;
  uint16_t duplicatesDropped = 0;
  uint16_t unreadableSkipped = 0;
  uint32_t walkMs = 0;
  uint32_t sortMs = 0;
  uint32_t totalMs = 0;
  uint64_t sdReadBytes = 0;
  uint64_t sdWrittenBytes = 0;
  uint32_t minimumFreeHeap = 0;  // low-water mark since boot
  uint16_t parsed = 0;
  uint16_t metadataReused = 0;
  uint16_t preparedSorts = 0;
  uint16_t sortPasses = 0;
  bool indexReplaced = false;
  // Reconciliation against the previous index. Their sum over a rebuild with no
  // card changes should be: unchanged == books, everything else zero.
  uint16_t unchanged = 0;  // same (name, size): keeps its place in "Recently added"
  uint16_t added = 0;      // matched nothing, not even by size
  uint16_t renamed = 0;    // matched a leftover entry by size alone
  uint16_t removed = 0;    // previous entry no book claimed
  uint16_t enriched = 0;   // took its title or author from the book rather than the filename
  bool dedupDegraded = false;
};

// Walk once, preserving arrival history separately from metadata freshness.
// forceRefresh bypasses the source cache, but never overrides readMetadata=false.
bool buildLibraryIndex(const char* rootPath, BuildStats& stats, bool readMetadata = false,
                       uint16_t requestedSorts = DEFAULT_SORTS, bool forceRefresh = false);
bool prepareLibraryOrders(uint16_t requestedSorts, BuildStats& stats);

// Live index path, shared by the builder and activity.
const char* libraryIndexPath();

}  // namespace library
