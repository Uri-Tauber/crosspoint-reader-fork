#pragma once

#include <Epub.h>
#include <Epub/ReaderRenderSpec.h>
#include <Logging.h>

#include <cstring>
#include <optional>

#include "ProgressFile.h"

namespace EpubReaderUtils {

// progress.bin is irreplaceable user state (the reader's place in the book), so unlike the
// regenerable section/book caches it is *migrated, never discarded* on a format change: an older
// record is still read, then upgraded on the next save. Legacy records had no version field and are
// detected by length (4 or 6 bytes); every versioned record leads with PROGRESS_VERSION.
//
// v1 layout (27 bytes, all little-endian; ints/floats copied raw -- device-local cache):
//   [0]      version (== PROGRESS_VERSION)
//   [1..2]   spineIndex   u16
//   [3..4]   page         u16
//   [5..6]   paragraphIndex u16   (PROGRESS_PARAGRAPH_UNKNOWN if the save site had no anchor)
//   [7..8]   totalPages   u16     (display-only: feeds the status bar before the section loads)
//   [9..26]  ReaderRenderSpec fields, in the same order Section serializes its header
constexpr uint8_t PROGRESS_VERSION = 1;
constexpr uint16_t PROGRESS_PARAGRAPH_UNKNOWN = 0xFFFF;
constexpr size_t PROGRESS_RECORD_SIZE = 27;

struct ProgressRecord {
  uint16_t spineIndex = 0;
  uint16_t page = 0;
  uint16_t paragraphIndex = PROGRESS_PARAGRAPH_UNKNOWN;
  uint16_t totalPages = 0;
  ReaderRenderSpec spec{};
};

// Usable only when it names a real paragraph. UNKNOWN = the save site had no LUT. 0 = "before the first
// <p>", which is the chapter top for a normal book but ALSO every page of a <div>-per-paragraph chapter
// (the layout parser counts only <p>/<li>), so 0 can't be trusted as a position. Both fall back to the
// saved absolute page rather than remapping to a bogus one (page 0, or a hang -- see the callers).
constexpr bool isUsableResumeAnchor(uint16_t paragraphIndex) {
  return paragraphIndex != PROGRESS_PARAGRAPH_UNKNOWN && paragraphIndex != 0;
}

// Ties PROGRESS_RECORD_SIZE to the layout by summing each field's on-wire size (not sizeof, which has
// padding), so a width change fails the build until encode/decode and the size agree. Does NOT catch an
// *added* ReaderRenderSpec field: when you add one, update encode/decodeProgress and this sum too, or it
// won't survive a save/reload despite operator== comparing it.
static_assert(PROGRESS_RECORD_SIZE ==
                  sizeof(PROGRESS_VERSION) + sizeof(ProgressRecord::spineIndex) + sizeof(ProgressRecord::page) +
                      sizeof(ProgressRecord::paragraphIndex) + sizeof(ProgressRecord::totalPages) +
                      sizeof(ReaderRenderSpec::fontId) + sizeof(ReaderRenderSpec::lineCompression) +
                      sizeof(uint8_t) /*extraParagraphSpacing*/ + sizeof(ReaderRenderSpec::paragraphAlignment) +
                      sizeof(ReaderRenderSpec::viewportWidth) + sizeof(ReaderRenderSpec::viewportHeight) +
                      sizeof(uint8_t) /*hyphenationEnabled*/ + sizeof(uint8_t) /*embeddedStyle*/ +
                      sizeof(ReaderRenderSpec::imageRendering) + sizeof(uint8_t) /*focusReadingEnabled*/,
              "PROGRESS_RECORD_SIZE out of sync with encodeProgress/decodeProgress layout");

inline void encodeProgress(const ProgressRecord& r, uint8_t* out) {
  size_t o = 0;
  const auto putU16 = [&](uint16_t v) {
    out[o++] = v & 0xFF;
    out[o++] = (v >> 8) & 0xFF;
  };
  const auto putRaw = [&](const void* src, size_t n) {
    memcpy(out + o, src, n);
    o += n;
  };
  out[o++] = PROGRESS_VERSION;
  putU16(r.spineIndex);
  putU16(r.page);
  putU16(r.paragraphIndex);
  putU16(r.totalPages);
  putRaw(&r.spec.fontId, sizeof(r.spec.fontId));
  putRaw(&r.spec.lineCompression, sizeof(r.spec.lineCompression));
  out[o++] = r.spec.extraParagraphSpacing ? 1 : 0;
  out[o++] = r.spec.paragraphAlignment;
  putU16(r.spec.viewportWidth);
  putU16(r.spec.viewportHeight);
  out[o++] = r.spec.hyphenationEnabled ? 1 : 0;
  out[o++] = r.spec.embeddedStyle ? 1 : 0;
  out[o++] = r.spec.imageRendering;
  out[o++] = r.spec.focusReadingEnabled ? 1 : 0;
}

// Precondition: the caller has verified `in` holds a PROGRESS_RECORD_SIZE-byte v1 record
// (in[0] == PROGRESS_VERSION). Reads are little-endian / raw memcpy, mirroring encodeProgress.
inline void decodeProgress(const uint8_t* in, ProgressRecord& r) {
  size_t o = 1;  // skip version byte (already validated by caller)
  const auto getU16 = [&]() -> uint16_t {
    const uint16_t v = static_cast<uint16_t>(in[o] | (in[o + 1] << 8));
    o += 2;
    return v;
  };
  const auto getRaw = [&](void* dst, size_t n) {
    memcpy(dst, in + o, n);
    o += n;
  };
  r.spineIndex = getU16();
  r.page = getU16();
  r.paragraphIndex = getU16();
  r.totalPages = getU16();
  getRaw(&r.spec.fontId, sizeof(r.spec.fontId));
  getRaw(&r.spec.lineCompression, sizeof(r.spec.lineCompression));
  r.spec.extraParagraphSpacing = in[o++] != 0;
  r.spec.paragraphAlignment = in[o++];
  r.spec.viewportWidth = getU16();
  r.spec.viewportHeight = getU16();
  r.spec.hyphenationEnabled = in[o++] != 0;
  r.spec.embeddedStyle = in[o++] != 0;
  r.spec.imageRendering = in[o++];
  r.spec.focusReadingEnabled = in[o++] != 0;
}

// Legacy writer (unversioned 6-byte record). Kept for callers without spec/paragraph context
// (e.g. KOReader sync return): the reader reopens under the same settings, so a plain resume is
// correct and the record self-heals to versioned on the next in-reader save.
inline bool saveProgress(const Epub& epub, int spineIndex, int pageNumber, int pageCount) {
  if (spineIndex < 0 || spineIndex > 0xFFFF || pageNumber < 0 || pageNumber > 0xFFFF || pageCount < 0 ||
      pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Progress values out of range: spine=%d page=%d count=%d", spineIndex, pageNumber, pageCount);
    return false;
  }
  uint8_t data[6];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = pageNumber & 0xFF;
  data[3] = (pageNumber >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  if (!ProgressFile::writeAtomic(epub.getCachePath(), data, sizeof(data))) {
    return false;
  }
  LOG_DBG("ERS", "Progress saved (legacy): spine=%d page=%d", spineIndex, pageNumber);
  return true;
}

// Versioned writer: persists the paragraph anchor and the render spec so a later resume can decide
// same-pagination (exact page) vs. re-paginated (remap by paragraph). Used by the reader's own saves.
inline bool saveProgress(const Epub& epub, int spineIndex, int pageNumber, int pageCount, uint16_t paragraphIndex,
                         const ReaderRenderSpec& spec) {
  if (spineIndex < 0 || spineIndex > 0xFFFF || pageNumber < 0 || pageNumber > 0xFFFF || pageCount < 0 ||
      pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Progress values out of range: spine=%d page=%d count=%d", spineIndex, pageNumber, pageCount);
    return false;
  }
  ProgressRecord rec;
  rec.spineIndex = static_cast<uint16_t>(spineIndex);
  rec.page = static_cast<uint16_t>(pageNumber);
  rec.paragraphIndex = paragraphIndex;
  rec.totalPages = static_cast<uint16_t>(pageCount);
  rec.spec = spec;
  uint8_t data[PROGRESS_RECORD_SIZE];
  encodeProgress(rec, data);
  if (!ProgressFile::writeAtomic(epub.getCachePath(), data, sizeof(data))) {
    return false;
  }
  LOG_DBG("ERS", "Progress saved: spine=%d page=%d para=%u", spineIndex, pageNumber, paragraphIndex);
  return true;
}

}  // namespace EpubReaderUtils
