#include "LibraryIndexFile.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "LibraryText.h"

namespace library {

LibraryIndexFile::~LibraryIndexFile() { close(); }

bool LibraryIndexFile::open(const char* path) {
  close();
  readFailed = false;
  if (!Storage.openFileForRead("LIBIDX", path, file)) {
    readFailed = true;
    return false;
  }

  if (file.read(&head, sizeof(head)) != static_cast<int>(sizeof(head))) {
    readFailed = true;
    lastValidity = ClixValidity::SizeMismatch;
    file.close();
    return false;
  }

  lastValidity = validateHeader(head, file.fileSize64());
  if (lastValidity != ClixValidity::Ok) {
    LOG_INF("LIBIDX", "index rejected: %s", clixValidityName(lastValidity));
    file.close();
    return false;
  }
  opened = true;
  return true;
}

void LibraryIndexFile::close() {
  if (file.isOpen()) file.close();
  opened = false;
}

bool LibraryIndexFile::readAt(const uint32_t offset, void* dst, const size_t len) {
  if (!opened) return false;
  // Every offset handed to this function comes from the header, and the header
  // was validated against the real file size, so a short read means the card
  // changed under us rather than a bad computation.
  if (!file.seekSet(offset)) return false;
  return file.read(dst, len) == static_cast<int>(len);
}

uint16_t LibraryIndexFile::ordinalForRow(const SortOrder order, const uint16_t row) {
  constexpr uint16_t NONE = 0xFFFF;
  if (!opened || row >= head.bookCount) return NONE;
  const auto kind = sortKind(order);
  if (!hasOrders(1u << static_cast<uint8_t>(kind))) return NONE;
  const uint16_t k = descending(order) ? head.bookCount - 1 - row : row;
  if (kind == SortKind::Title) return k;
  const uint32_t offset = kind == SortKind::Added    ? arrivalOrderOffset(head, k)
                          : kind == SortKind::Author ? authorOrderOffset(head, k)
                                                     : metadataOrderOffset(head, kind, k);
  uint16_t ordinal = NONE;
  return readAt(offset, &ordinal, sizeof(ordinal)) && ordinal < head.bookCount ? ordinal : NONE;
}

bool LibraryIndexFile::readMetadata(uint16_t ordinal, CachedMetadata& out) {
  if (!opened || ordinal >= head.bookCount ||
      !readAt(head.metadataStart + static_cast<uint32_t>(ordinal) * sizeof(out), &out, sizeof(out)))
    return false;
  if (out.extracted > 1 || out.titleFromBook > 1 || out.calibreIndex > 1 || out.seriesKey.valid > 1 ||
      out.seriesKey.subdivision > 1 || out.seriesKey.calibre > 1 || out.seriesKey.dotted[31] ||
      !std::isfinite(out.seriesKey.major) || !std::isfinite(out.seriesKey.decimal))
    return false;
  if (out.originalAuthor[sizeof(out.originalAuthor) - 1] || out.title[sizeof(out.title) - 1] ||
      out.seriesIndex[sizeof(out.seriesIndex) - 1])
    return false;
  for (unsigned i = 0; i < METADATA_SORT_COUNT; ++i)
    if (out.values[i][127] || out.normalized[i][SORT_TEXT_BYTES - 1]) return false;
  return true;
}

bool LibraryIndexFile::readRecord(const uint16_t ordinal, ClixRecord& out) {
  if (!opened || ordinal >= head.bookCount) return false;
  if (!readAt(recordOffset(head, ordinal), &out, sizeof(out))) return false;

  // Clamp here, at the single point every record enters the program. These
  // lengths come off an SD card that the user can write to and that can rot: a
  // foldLen of 255 against a 96-byte field sends a string_view 159 bytes past the
  // end of the record, and callers build views from them without looking. Fixing
  // it at each call site would mean fixing it again at the next one.
  out.foldLen = static_cast<uint8_t>(std::min<size_t>(out.foldLen, CLIX_FOLD_BYTES));
  out.authorKeyLen = static_cast<uint8_t>(std::min<size_t>(out.authorKeyLen, CLIX_AUTHOR_KEY_BYTES));
  // nameOff is u32 and every reader adds a length to it before comparing against
  // the section size. A forged value near the top of the range wraps that sum and
  // passes the bounds check it was supposed to fail, so it is rejected here
  // instead — the one place that can, before any arithmetic touches it.
  if (out.nameOff > head.nameLen) {
    out.nameLen = 0;
    out.nameOff = 0;
  }
  return true;
}

bool LibraryIndexFile::readName(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened || record.nameLen == 0) return false;
  if (record.nameOff + record.nameLen > head.nameLen) return false;
  out.resize(record.nameLen);
  return readAt(head.nameStart + record.nameOff, out.data(), record.nameLen);
}

bool LibraryIndexFile::readDisplay(const ClixRecord& record, SortKind kind, std::string& title, std::string& value) {
  value.clear();
  if (!readName(record, title)) return false;
  const unsigned selected = kind >= SortKind::Date ? 2 + static_cast<unsigned>(kind) - 3 : 0;
  uint32_t at = record.nameOff + record.nameLen;
  for (unsigned field = 0; field <= std::max(1u, selected); ++field) {
    uint8_t len = 0;
    if (at >= head.nameLen || !readAt(head.nameStart + at, &len, 1)) return false;
    ++at;
    if (len > head.nameLen - at) return false;
    if (field == selected || (field == 1 && len)) {
      auto& target = field == 1 ? title : value;
      target.resize(len);
      if (len && !readAt(head.nameStart + at, target.data(), len)) return false;
    }
    at += len;
  }
  return true;
}

bool LibraryIndexFile::readAuthor(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened || record.nameLen == 0) return false;
  const uint32_t lenAt = record.nameOff + record.nameLen;
  if (lenAt + 1 > head.nameLen) return false;

  uint8_t authorLen = 0;
  if (!readAt(head.nameStart + lenAt, &authorLen, sizeof(authorLen))) return false;
  if (authorLen == 0) return false;
  if (lenAt + 1 + authorLen > head.nameLen) return false;

  out.resize(authorLen);
  return readAt(head.nameStart + lenAt + 1, out.data(), authorLen);
}

// The book's own title, after the name and the author. Absent (length 0) for a
// book that never told us one, in which case the caller shows the filename.
bool LibraryIndexFile::readTitle(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened || record.nameLen == 0) return false;
  uint32_t at = record.nameOff + record.nameLen;
  if (at + 1 > head.nameLen) return false;

  uint8_t authorLen = 0;
  if (!readAt(head.nameStart + at, &authorLen, sizeof(authorLen))) return false;
  at += 1 + authorLen;
  if (at + 1 > head.nameLen) return false;

  uint8_t titleLen = 0;
  if (!readAt(head.nameStart + at, &titleLen, sizeof(titleLen))) return false;
  if (titleLen == 0 || at + 1 + titleLen > head.nameLen) return false;

  out.resize(titleLen);
  return readAt(head.nameStart + at + 1, out.data(), titleLen);
}

bool LibraryIndexFile::readSortValue(const ClixRecord& record, const SortKind kind, std::string& out) {
  out.clear();
  if (!opened || kind < SortKind::Date || kind >= SortKind::Count || record.nameOff > head.nameLen ||
      record.nameLen > head.nameLen - record.nameOff)
    return false;
  uint32_t at = record.nameOff + record.nameLen;
  const unsigned field = 2 + static_cast<unsigned>(kind) - static_cast<unsigned>(SortKind::Date);
  for (unsigned i = 0; i <= field; ++i) {
    uint8_t len = 0;
    if (at >= head.nameLen || !readAt(head.nameStart + at, &len, 1)) return false;
    ++at;
    if (len > head.nameLen - at) return false;
    if (i == field) {
      out.resize(len);
      return len == 0 || readAt(head.nameStart + at, out.data(), len);
    }
    at += len;
  }
  return false;
}

bool LibraryIndexFile::readPath(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened || record.folderId >= head.folderCount) return false;

  // Folder records are variable length, so reaching folder n means walking the
  // n preceding length bytes. At one seek per folder this is only done when a
  // book is opened or its details are shown, never while paging.
  uint32_t offset = head.folderStart;
  for (uint16_t i = 0; i <= record.folderId; i++) {
    uint8_t pathLen = 0;
    if (!readAt(offset, &pathLen, sizeof(pathLen)) || pathLen == 0) return false;
    if (offset >= head.folderStart + head.folderLen || pathLen > head.folderStart + head.folderLen - offset - 1)
      return false;
    if (i == record.folderId) {
      std::string dir(pathLen, '\0');
      if (!readAt(offset + 1, dir.data(), pathLen)) return false;
      std::string name;
      if (!readName(record, name)) return false;
      out = joinLibraryPath(dir, name);
      return true;
    }
    offset += 1u + pathLen;
    if (offset >= head.folderStart + head.folderLen) return false;
  }
  return false;
}

}  // namespace library
