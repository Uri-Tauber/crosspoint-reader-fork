#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

/// Pre-indexes every spine item (chapter) in every EPUB on the SD card so that
/// the user doesn't see the "Indexing" popup when opening chapters for the
/// first time. Follows the ClearCacheActivity pattern: WARNING → INDEXING →
/// SUCCESS / FAILED.
class IndexLibraryActivity final : public Activity {
 public:
  explicit IndexLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("IndexLibrary", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, INDEXING, SUCCESS, FAILED };

  State state = WARNING;

  void goBack() { finish(); }

  // Progress tracking
  int totalBooks = 0;
  int processedBooks = 0;
  int indexedChapters = 0;
  int skippedChapters = 0;
  int failedChapters = 0;
  std::string currentBookName;

  void indexLibrary();
  void findEpubFiles(const char* dirPath, std::vector<std::string>& outPaths);
  void computeViewport(uint16_t& outWidth, uint16_t& outHeight);
};
