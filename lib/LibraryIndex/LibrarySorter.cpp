#include "LibrarySorter.h"

#include <Arduino.h>
#include <Logging.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace library {
namespace {
constexpr char RUN_A[] = "/.crosspoint/library.sort-a";
constexpr char RUN_B[] = "/.crosspoint/library.sort-b";
constexpr unsigned INITIAL_RUN = 8;
constexpr unsigned MERGE_BUFFER = 4;
constexpr unsigned WORKSPACE_SLOTS = 12;
struct ScratchCleanup {
  ~ScratchCleanup() {
    Storage.remove(RUN_A);
    Storage.remove(RUN_B);
  }
};
size_t alignUp(const size_t value, const size_t alignment) { return (value + alignment - 1) / alignment * alignment; }
uint8_t* slot(SortWorkspace& workspace, const size_t stride, const unsigned index) {
  return workspace.bytes + stride * index;
}
uint16_t ordinalOf(const uint8_t* record, const size_t keySize) {
  uint16_t ordinal;
  memcpy(&ordinal, record + keySize, sizeof(ordinal));
  return ordinal;
}
void setOrdinal(uint8_t* record, const size_t keySize, const uint16_t ordinal) {
  memcpy(record + keySize, &ordinal, sizeof(ordinal));
}
bool before(const uint8_t* a, const uint8_t* b, const uint8_t field, const size_t keySize,
            const CompareSortKeys compare) {
  const int result = compare(a, b, field);
  return result < 0 || (result == 0 && ordinalOf(a, keySize) < ordinalOf(b, keySize));
}
struct RunReader {
  HalFile& file;
  uint8_t* buffer;
  size_t stride;
  unsigned next, end, at = 0, size = 0;
  bool available() const { return at < size || next < end; }
  uint8_t* current() { return buffer + at * stride; }
  bool fill() {
    if (at < size || next == end) return true;
    size = std::min(MERGE_BUFFER, end - next);
    at = 0;
    if (!file.seekSet(next * stride) || file.read(buffer, size * stride) != static_cast<int>(size * stride))
      return false;
    next += size;
    return true;
  }
};
}  // namespace
int compareMetadataSortKeys(const void* left, const void* right, const uint8_t field) {
  const auto& a = *static_cast<const BufferedSortKey*>(left);
  const auto& b = *static_cast<const BufferedSortKey*>(right);
  int cmp;
  if (field == 0) {
    cmp = !a.date != !b.date ? (!a.date ? 1 : -1) : a.date < b.date ? -1 : a.date > b.date ? 1 : 0;
  } else {
    cmp = !a.text[0] != !b.text[0] ? (!a.text[0] ? 1 : -1) : strcmp(a.text, b.text);
    if (!cmp && field == 3 && a.text[0]) cmp = compareSeriesKeys(a.series, b.series);
  }
  return cmp;
}
bool bufferedSort(const uint16_t count, const uint8_t field, const size_t keySize, const LoadSortKey load,
                  const CompareSortKeys compare, void* context, SortWorkspace& workspace, uint16_t* order,
                  uint16_t& passes) {
  if (count == 0) return true;
  if (!keySize || !load || !compare || !order) return false;
  const size_t stride = alignUp(keySize + sizeof(uint16_t), alignof(std::max_align_t));
  if (stride * WORKSPACE_SLOTS > sizeof(workspace.bytes)) return false;
  if (count == 1) {
    if (!load(context, 0, field, workspace.bytes)) return false;
    order[0] = 0;
    return true;
  }
  ScratchCleanup cleanup;
  const char* source = RUN_A;
  const char* target = RUN_B;
  HalFile output;
  if (!Storage.openFileForWrite("LIBIDX", source, output)) return false;
  for (unsigned start = 0; start < count; start += INITIAL_RUN) {
    const unsigned size = std::min<unsigned>(INITIAL_RUN, count - start);
    for (unsigned i = 0; i < size; ++i) {
      uint8_t* record = slot(workspace, stride, i);
      if (!load(context, start + i, field, record)) return false;
      setOrdinal(record, keySize, start + i);
    }
    for (unsigned i = 1; i < size; ++i) {
      for (unsigned j = i;
           j && before(slot(workspace, stride, j), slot(workspace, stride, j - 1), field, keySize, compare); --j) {
        memcpy(slot(workspace, stride, INITIAL_RUN), slot(workspace, stride, j), stride);
        memcpy(slot(workspace, stride, j), slot(workspace, stride, j - 1), stride);
        memcpy(slot(workspace, stride, j - 1), slot(workspace, stride, INITIAL_RUN), stride);
      }
    }
    if (output.write(workspace.bytes, size * stride) != size * stride) return false;
    delay(1);
  }
  if (!output.close()) return false;
  ++passes;
  for (unsigned width = INITIAL_RUN; width < count; width *= 2) {
    HalFile input;
    if (!Storage.openFileForRead("LIBIDX", source, input) || !Storage.openFileForWrite("LIBIDX", target, output))
      return false;
    for (unsigned start = 0; start < count; start += 2 * width) {
      const unsigned middle = std::min<unsigned>(start + width, count);
      const unsigned end = std::min<unsigned>(start + 2 * width, count);
      RunReader left{input, slot(workspace, stride, 0), stride, start, middle};
      RunReader right{input, slot(workspace, stride, MERGE_BUFFER), stride, middle, end};
      unsigned pending = 0;
      while (left.available() || right.available()) {
        if (!left.fill() || !right.fill()) return false;
        const bool takeLeft = !right.available() ||
                              (left.available() && before(left.current(), right.current(), field, keySize, compare));
        memcpy(slot(workspace, stride, INITIAL_RUN + pending++), takeLeft ? left.current() : right.current(), stride);
        if (takeLeft)
          ++left.at;
        else
          ++right.at;
        if (pending == MERGE_BUFFER || (!left.available() && !right.available())) {
          const size_t bytes = pending * stride;
          if (output.write(slot(workspace, stride, INITIAL_RUN), bytes) != bytes) return false;
          pending = 0;
        }
      }
      delay(1);
    }
    if (!output.close()) return false;
    std::swap(source, target);
    ++passes;
  }
  HalFile input;
  if (!Storage.openFileForRead("LIBIDX", source, input)) return false;
  for (unsigned start = 0; start < count; start += INITIAL_RUN) {
    const unsigned size = std::min<unsigned>(INITIAL_RUN, count - start);
    const size_t bytes = size * stride;
    if (input.read(workspace.bytes, bytes) != static_cast<int>(bytes)) return false;
    for (unsigned i = 0; i < size; ++i) order[start + i] = ordinalOf(slot(workspace, stride, i), keySize);
  }
  return true;
}
}  // namespace library
