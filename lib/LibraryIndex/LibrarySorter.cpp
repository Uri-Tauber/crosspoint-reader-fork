#include "LibrarySorter.h"

#include <Arduino.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace library {
namespace {
constexpr char RUN_A[] = "/.crosspoint/library.sort-a";
constexpr char RUN_B[] = "/.crosspoint/library.sort-b";
struct ScratchCleanup {
  ~ScratchCleanup() {
    Storage.remove(RUN_A);
    Storage.remove(RUN_B);
  }
};
struct RunReader {
  HalFile& file;
  BufferedSortKey* buffer;
  unsigned next, end, at = 0, size = 0;
  bool available() const { return at < size || next < end; }
  bool fill() {
    if (at < size || next == end) return true;
    size = std::min(4u, end - next);
    at = 0;
    if (!file.seekSet(next * sizeof(BufferedSortKey)) ||
        file.read(buffer, size * sizeof(BufferedSortKey)) != static_cast<int>(size * sizeof(BufferedSortKey)))
      return false;
    next += size;
    return true;
  }
};
}  // namespace
bool sortKeyBefore(const BufferedSortKey& a, const BufferedSortKey& b, uint8_t field) {
  int cmp;
  if (field == 0) {
    cmp = !a.date != !b.date ? (!a.date ? 1 : -1) : a.date < b.date ? -1 : a.date > b.date ? 1 : 0;
  } else {
    cmp = !a.text[0] != !b.text[0] ? (!a.text[0] ? 1 : -1) : strcmp(a.text, b.text);
    if (!cmp && field == 3 && a.text[0]) cmp = compareSeriesKeys(a.series, b.series);
  }
  return cmp < 0 || (!cmp && a.ordinal < b.ordinal);
}
bool bufferedSort(uint16_t count, uint8_t field, LoadSortKey load, void* context, SortWorkspace& workspace,
                  uint16_t* order, uint16_t& passes) {
  ScratchCleanup cleanup;
  const char* source = RUN_A;
  const char* target = RUN_B;
  HalFile output;
  if (!Storage.openFileForWrite("LIBIDX", source, output)) return false;
  for (unsigned start = 0; start < count; start += 8) {
    const unsigned size = std::min(8u, count - start);
    for (unsigned i = 0; i < size; ++i) {
      if (!load(context, start + i, field, workspace.keys[i])) return false;
      workspace.keys[i].ordinal = start + i;
    }
    for (unsigned i = 1; i < size; ++i) {
      for (unsigned j = i; j && sortKeyBefore(workspace.keys[j], workspace.keys[j - 1], field); --j) {
        workspace.keys[8] = workspace.keys[j];
        workspace.keys[j] = workspace.keys[j - 1];
        workspace.keys[j - 1] = workspace.keys[8];
      }
    }
    if (output.write(reinterpret_cast<const uint8_t*>(workspace.keys), size * sizeof(BufferedSortKey)) !=
        size * sizeof(BufferedSortKey))
      return false;
    delay(1);
  }
  if (!output.close()) return false;
  ++passes;
  for (unsigned width = 8; width < count; width *= 2) {
    HalFile input;
    if (!Storage.openFileForRead("LIBIDX", source, input) || !Storage.openFileForWrite("LIBIDX", target, output))
      return false;
    for (unsigned start = 0; start < count; start += 2 * width) {
      const unsigned middle = std::min<unsigned>(start + width, count);
      const unsigned end = std::min<unsigned>(start + 2 * width, count);
      RunReader left{input, workspace.keys, start, middle};
      RunReader right{input, workspace.keys + 4, middle, end};
      unsigned pending = 0;
      while (left.available() || right.available()) {
        if (!left.fill() || !right.fill()) return false;
        const bool takeLeft = !right.available() ||
                              (left.available() && sortKeyBefore(left.buffer[left.at], right.buffer[right.at], field));
        workspace.keys[8 + pending++] = takeLeft ? left.buffer[left.at++] : right.buffer[right.at++];
        if (pending == 4 || (!left.available() && !right.available())) {
          const size_t bytes = pending * sizeof(BufferedSortKey);
          if (output.write(reinterpret_cast<const uint8_t*>(workspace.keys + 8), bytes) != bytes) return false;
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
  for (unsigned start = 0; start < count; start += 8) {
    const unsigned size = std::min(8u, count - start);
    const size_t bytes = size * sizeof(BufferedSortKey);
    if (input.read(workspace.keys, bytes) != static_cast<int>(bytes)) return false;
    for (unsigned i = 0; i < size; ++i) order[start + i] = workspace.keys[i].ordinal;
  }
  return true;
}
}  // namespace library
