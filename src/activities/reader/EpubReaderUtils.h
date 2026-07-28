#pragma once

#include <Epub.h>
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <vector>

#include "ProgressFile.h"

namespace EpubReaderUtils {

// Persists reader progress for an EPUB to its cache directory. Returns true on success.
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
  LOG_DBG("ERS", "Progress saved: spine=%d page=%d", spineIndex, pageNumber);
  return true;
}

// --- Tappable links on the displayed page ------------------------------------------------
//
// The parser underlines every internal <a href> it turns into a footnote entry, and per-word
// style bits are part of the cached page, so the underlined words already ARE the link's
// geometry -- nothing extra is stored on the SD card for this. collectLinkBoxes rebuilds a
// link's on-screen box from them after each render, and linkBoxAtPoint hit-tests a tap.

// One tappable link on the page being displayed, in screen coordinates.
struct LinkBox {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
  uint8_t footnoteIndex;  // index into the page's footnote entries
};

namespace detail {

constexpr size_t FOLD_CAPACITY = FOOTNOTE_NUMBER_LEN + 8;
// Below this length a label must match a run exactly. Note markers are short ("1", "12", "*"),
// where a prefix rule would let the run "12" claim the entry labelled "1".
constexpr size_t PREFIX_MATCH_MIN = 4;

// Characters a fold drops -- only what a label and the words laid out from the same markup can
// legitimately disagree on: layout splits a link's text on spaces (which the fold rejoins), may
// insert a hyphenation '-' at a line break, and focus reading splits a word into tokens.
inline bool foldDrops(const uint8_t c) { return std::isspace(c) != 0 || c == '-'; }

// Appends the lowercased fold of `text` to out[used..] (NUL-terminated) and returns the new
// length. Comparing folds is what lets a link's laid-out words be matched to the label the
// parser collected from the markup. Non-ASCII bytes are kept verbatim so non-Latin labels (and
// dagger markers) still compare.
inline size_t appendFold(const char* text, char* out, const size_t outSize, size_t used) {
  for (const auto* p = reinterpret_cast<const uint8_t*>(text); *p != 0 && used + 1 < outSize; p++) {
    if (*p < 0x80) {
      if (foldDrops(*p)) continue;
      out[used++] = static_cast<char>(std::tolower(*p));
    } else {
      out[used++] = static_cast<char>(*p);
    }
  }
  out[used] = '\0';
  return used;
}

}  // namespace detail

// Rebuilds the screen box of every tappable link on `page`, writing up to outCap of them and
// returning how many. Underlined runs and footnote entries both come out in document order, but
// they are paired by label rather than by position: one stray <u> in the body text would
// otherwise skew every link after it on the page. A run that matches no label yields no box,
// and an entry that gets no box stays reachable through the footnotes menu.
inline uint8_t collectLinkBoxes(const Page& page, const std::vector<FootnoteEntry>& footnotes,
                                const GfxRenderer& renderer, const int fontId, const int marginLeft,
                                const int marginTop, LinkBox* out, const uint8_t outCap) {
  if (footnotes.empty() || outCap == 0) return 0;

  const int lineHeight = renderer.getLineHeight(fontId);
  const int ascender = renderer.getFontAscenderSize(fontId);
  const size_t entryCount = std::min<size_t>(footnotes.size(), Page::MAX_FOOTNOTES_PER_PAGE);
  static_assert(Page::MAX_FOOTNOTES_PER_PAGE <= 16, "paired bitmask holds one bit per footnote");
  uint16_t paired = 0;  // one bit per footnote entry that already has a box
  uint8_t count = 0;

  for (const auto& element : page.elements) {
    if (count >= outCap) break;
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto& block = line->getBlock();
    if (!block || !block->valid()) continue;
    const int rubyShift = block->getRubyShift(ascender);

    for (uint16_t i = 0; i < block->wordCount() && count < outCap;) {
      if ((block->wordStyle(i) & EpdFontFamily::UNDERLINE) == 0) {
        i++;
        continue;
      }

      // One run: the consecutive underlined words, their combined extent and folded text.
      char fold[detail::FOLD_CAPACITY] = {};
      size_t foldLen = 0;
      int left = INT_MAX;
      int right = INT_MIN;
      int topLift = 0;
      uint16_t runEnd = i;
      while (runEnd < block->wordCount() && (block->wordStyle(runEnd) & EpdFontFamily::UNDERLINE) != 0) {
        const auto style = block->wordStyle(runEnd);
        int advance = renderer.getTextAdvanceX(fontId, block->wordText(runEnd), style);
        // SUP/SUB glyphs are drawn at half scale and TextBlock::render halves their underline
        // the same way, so a superscript note marker's box must be halved to match the pixels.
        if ((style & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
          advance = (advance + 1) / 2;
        }
        // A superscript marker -- what most noterefs are -- is drawn above the line's top by
        // 40% of the ascender, so grow the box upwards to actually cover the glyph.
        if ((style & EpdFontFamily::SUP) != 0) {
          topLift = std::max(topLift, ascender * 2 / 5);
        }
        // min/max rather than first/last word: a mixed-direction line is stored in visual
        // order, so a run's words do not necessarily run left to right.
        left = std::min<int>(left, block->wordXpos(runEnd));
        right = std::max<int>(right, block->wordXpos(runEnd) + advance);
        foldLen = detail::appendFold(block->wordText(runEnd), fold, sizeof(fold), foldLen);
        runEnd++;
      }
      i = runEnd;
      if (foldLen == 0 || right <= left) continue;

      for (size_t entry = 0; entry < entryCount; entry++) {
        if ((paired & static_cast<uint16_t>(1u << entry)) != 0) continue;
        char labelFold[detail::FOLD_CAPACITY] = {};
        const size_t labelLen = detail::appendFold(footnotes[entry].number, labelFold, sizeof(labelFold), 0);
        if (labelLen == 0) continue;
        // A long label is cross-reference text, which can be truncated at collection time or
        // split across two lines (only the first line's words reach this run), so a prefix is
        // the best available signal there. Short markers must match exactly.
        const bool exact = labelLen == foldLen && memcmp(fold, labelFold, labelLen) == 0;
        const bool prefix = labelLen >= detail::PREFIX_MATCH_MIN && foldLen >= detail::PREFIX_MATCH_MIN &&
                            memcmp(fold, labelFold, std::min(labelLen, foldLen)) == 0;
        if (!exact && !prefix) continue;

        out[count].x = static_cast<int16_t>(line->xPos + left + marginLeft);
        out[count].y = static_cast<int16_t>(line->yPos + rubyShift + marginTop - topLift);
        out[count].width = static_cast<int16_t>(right - left);
        out[count].height = static_cast<int16_t>(lineHeight + topLift);
        out[count].footnoteIndex = static_cast<uint8_t>(entry);
        count++;
        paired |= static_cast<uint16_t>(1u << entry);
        break;
      }
    }
  }
  return count;
}

// Index into the page's footnotes of the link whose box contains (x, y), or -1 when the tap
// missed every link.
inline int linkBoxAtPoint(const LinkBox* boxes, const uint8_t count, const int x, const int y) {
  // Finger slop, plus a floor on the target width: a note marker is often a single superscript
  // digit only a few pixels wide. The box is never grown vertically beyond its own line, so
  // taps on the lines above and below still reach the page-turn zones.
  constexpr int TOUCH_SLOP = 6;
  constexpr int MIN_TOUCH_WIDTH = 28;
  for (uint8_t i = 0; i < count; i++) {
    const LinkBox& box = boxes[i];
    const int slopX = std::max<int>(TOUCH_SLOP, (MIN_TOUCH_WIDTH - box.width) / 2);
    if (x >= box.x - slopX && x < box.x + box.width + slopX && y >= box.y - TOUCH_SLOP &&
        y < box.y + box.height + TOUCH_SLOP) {
      return box.footnoteIndex;
    }
  }
  return -1;
}

}  // namespace EpubReaderUtils
