#include "TxtPageIndex.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace {

template <typename T>
bool readExact(HalFile& file, T& value) {
  return file.read(&value, sizeof(value)) == sizeof(value);
}

template <typename T>
bool writeExact(HalFile& file, const T& value) {
  return file.write(&value, sizeof(value)) == sizeof(value);
}

bool sameSpec(const TxtIndexSpec& lhs, const TxtIndexSpec& rhs) {
  return lhs.sourceFileSize == rhs.sourceFileSize && lhs.viewportWidth == rhs.viewportWidth &&
         lhs.linesPerPage == rhs.linesPerPage && lhs.fontId == rhs.fontId && lhs.screenMargin == rhs.screenMargin &&
         lhs.paragraphAlignment == rhs.paragraphAlignment;
}

}  // namespace

TxtPageIndex::~TxtPageIndex() { close(); }

bool TxtPageIndex::openOrStart(const char* cachePath, const TxtIndexSpec& newSpec) {
  close();
  spec = newSpec;

  const int finalLength = snprintf(finalPath, sizeof(finalPath), "%s/index.bin", cachePath);
  const int partialLength = snprintf(partialPath, sizeof(partialPath), "%s/index.bin.part", cachePath);
  if (finalLength < 0 || static_cast<size_t>(finalLength) >= sizeof(finalPath) || partialLength < 0 ||
      static_cast<size_t>(partialLength) >= sizeof(partialPath)) {
    LOG_ERR("TXI", "TXT cache path is too long");
    return false;
  }

  if (readCompleteCache()) {
    Storage.remove(partialPath);
    return true;
  }
  return startBuild();
}

bool TxtPageIndex::readCompleteCache() {
  if (!Storage.openFileForRead("TXI", finalPath, file)) return false;

  uint8_t state = STATE_INCOMPLETE;
  TxtIndexSpec cachedSpec{};
  uint32_t cachedPageCount = 0;
  const bool headerValid = readHeader(state, cachedSpec, cachedPageCount);
  const uint64_t expectedSize = HEADER_SIZE + static_cast<uint64_t>(cachedPageCount) * sizeof(uint32_t);
  if (!headerValid || state != STATE_COMPLETE || !sameSpec(spec, cachedSpec) ||
      (spec.sourceFileSize > 0 && cachedPageCount == 0) || file.fileSize64() != expectedSize) {
    file.close();
    return false;
  }

  pageCount = cachedPageCount;
  complete = true;
  LOG_DBG("TXI", "Loaded TXT page index: %u pages", static_cast<unsigned>(pageCount));
  return true;
}

bool TxtPageIndex::startBuild() {
  Storage.remove(partialPath);
  if (!Storage.openFileForWrite("TXI", partialPath, file)) {
    LOG_ERR("TXI", "Failed to create partial TXT page index");
    return false;
  }

  pageCount = 0;
  building = true;
  if (!writeHeader(STATE_INCOMPLETE)) {
    close();
    return false;
  }
  if (spec.sourceFileSize > 0 && !appendPageOffset(0)) {
    close();
    return false;
  }
  LOG_DBG("TXI", "Started incremental TXT page index");
  return true;
}

bool TxtPageIndex::appendPageOffset(const uint32_t offset) {
  if (!building || offset >= spec.sourceFileSize) return false;
  const uint64_t recordPosition = HEADER_SIZE + static_cast<uint64_t>(pageCount) * sizeof(uint32_t);
  if (recordPosition > SIZE_MAX || !file.seekSet(static_cast<size_t>(recordPosition)) || !writeExact(file, offset)) {
    LOG_ERR("TXI", "Failed to append TXT page offset");
    return false;
  }
  pageCount++;
  return true;
}

bool TxtPageIndex::pageOffset(const uint32_t page, uint32_t& offset) {
  offset = 0;
  if (!file.isOpen() || page >= pageCount) return false;
  const uint64_t recordPosition = HEADER_SIZE + static_cast<uint64_t>(page) * sizeof(uint32_t);
  if (recordPosition > SIZE_MAX || !file.seekSet(static_cast<size_t>(recordPosition)) || !readExact(file, offset) ||
      offset >= spec.sourceFileSize) {
    LOG_ERR("TXI", "Failed to read TXT page offset %u", static_cast<unsigned>(page));
    return false;
  }
  return true;
}

bool TxtPageIndex::finish() {
  if (complete) return true;
  if (!building || !file.seekSet(0) || !writeHeader(STATE_COMPLETE)) {
    LOG_ERR("TXI", "Failed to finalize TXT page index header");
    return false;
  }
  file.flush();
  file.close();

  Storage.remove(finalPath);
  if (!Storage.rename(partialPath, finalPath)) {
    LOG_ERR("TXI", "Failed to publish completed TXT page index");
    return false;
  }
  building = false;
  if (!Storage.openFileForRead("TXI", finalPath, file)) {
    LOG_ERR("TXI", "Failed to reopen completed TXT page index");
    return false;
  }
  complete = true;
  LOG_DBG("TXI", "Completed TXT page index: %u pages", static_cast<unsigned>(pageCount));
  return true;
}

void TxtPageIndex::close() {
  if (file.isOpen()) file.close();
  if (building && partialPath[0] != '\0') Storage.remove(partialPath);
  pageCount = 0;
  complete = false;
  building = false;
}

bool TxtPageIndex::writeHeader(const uint8_t state) {
  constexpr uint16_t RESERVED = 0;
  constexpr uint8_t PADDING[3] = {};
  return writeExact(file, CACHE_MAGIC) && writeExact(file, CACHE_VERSION) && writeExact(file, state) &&
         writeExact(file, RESERVED) && writeExact(file, spec.sourceFileSize) && writeExact(file, spec.viewportWidth) &&
         writeExact(file, spec.linesPerPage) && writeExact(file, spec.fontId) && writeExact(file, spec.screenMargin) &&
         writeExact(file, spec.paragraphAlignment) && file.write(PADDING, sizeof(PADDING)) == sizeof(PADDING) &&
         writeExact(file, pageCount);
}

bool TxtPageIndex::readHeader(uint8_t& state, TxtIndexSpec& cachedSpec, uint32_t& cachedPageCount) {
  uint32_t magic = 0;
  uint8_t version = 0;
  uint16_t reserved = 0;
  uint8_t padding[3]{};
  return readExact(file, magic) && magic == CACHE_MAGIC && readExact(file, version) && version == CACHE_VERSION &&
         readExact(file, state) && readExact(file, reserved) && readExact(file, cachedSpec.sourceFileSize) &&
         readExact(file, cachedSpec.viewportWidth) && readExact(file, cachedSpec.linesPerPage) &&
         readExact(file, cachedSpec.fontId) && readExact(file, cachedSpec.screenMargin) &&
         readExact(file, cachedSpec.paragraphAlignment) && file.read(padding, sizeof(padding)) == sizeof(padding) &&
         readExact(file, cachedPageCount);
}
