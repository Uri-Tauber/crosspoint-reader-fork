#pragma once

#include <HalStorage.h>

#include <cstdint>

struct TxtIndexSpec {
  uint32_t sourceFileSize = 0;
  int32_t viewportWidth = 0;
  int32_t linesPerPage = 0;
  int32_t fontId = 0;
  int32_t screenMargin = 0;
  uint8_t paragraphAlignment = 0;
};

class TxtPageIndex final {
 public:
  TxtPageIndex() = default;
  ~TxtPageIndex();

  TxtPageIndex(const TxtPageIndex&) = delete;
  TxtPageIndex& operator=(const TxtPageIndex&) = delete;

  bool openOrStart(const char* cachePath, const TxtIndexSpec& spec);
  bool appendPageOffset(uint32_t offset);
  bool pageOffset(uint32_t page, uint32_t& offset);
  uint32_t knownPageCount() const { return pageCount; }
  bool isComplete() const { return complete; }
  bool finish();
  void close();

 private:
  static constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
  static constexpr uint8_t CACHE_VERSION = 4;
  static constexpr uint8_t STATE_INCOMPLETE = 0;
  static constexpr uint8_t STATE_COMPLETE = 1;
  static constexpr uint32_t HEADER_SIZE = 36;
  static constexpr size_t PATH_CAPACITY = 80;

  bool readCompleteCache();
  bool startBuild();
  bool writeHeader(uint8_t state);
  bool readHeader(uint8_t& state, TxtIndexSpec& cachedSpec, uint32_t& cachedPageCount);

  HalFile file;
  TxtIndexSpec spec{};
  char finalPath[PATH_CAPACITY]{};
  char partialPath[PATH_CAPACITY]{};
  uint32_t pageCount = 0;
  bool complete = false;
  bool building = false;
};
