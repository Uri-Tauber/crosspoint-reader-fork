#include "ScriptDetector.h"

#include <Utf8.h>

#include <algorithm>

namespace ScriptDetector {

bool containsHebrew(const char* text) {
  if (!text) return false;
  auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    uint32_t cp = utf8NextCodepoint(&p);
    if (isHebrewCodepoint(cp)) return true;
  }
  return false;
}

bool startsWithRtl(const char* text, int maxChars) {
  if (!text || maxChars <= 0) return false;
  auto* p = reinterpret_cast<const unsigned char*>(text);
  int count = 0;
  while (*p && count < maxChars) {
    uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0 || cp == REPLACEMENT_GLYPH) break;
    // Skip whitespace and combining marks — they don't indicate script
    if (cp <= 0x20) continue;
    if (utf8IsCombiningMark(cp)) continue;
    if (isRtlCodepoint(cp)) return true;
    count++;
  }
  return false;
}

// Encode a single Unicode codepoint as UTF-8, appending to `out`.
static void utf8Encode(uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

void reverseIfRtl(std::string& word) {
  if (word.empty()) return;

  // First pass: decode codepoints and check if any are RTL
  uint32_t codepoints[64];  // stack buffer — words longer than 64 codepoints are rare
  size_t count = 0;
  bool hasRtl = false;

  auto* p = reinterpret_cast<const unsigned char*>(word.c_str());
  while (*p && count < 64) {
    uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0 || cp == REPLACEMENT_GLYPH) break;
    if (isRtlCodepoint(cp)) hasRtl = true;
    codepoints[count++] = cp;
  }

  if (!hasRtl || count <= 1) return;

  // Reverse the codepoints array
  std::reverse(codepoints, codepoints + count);

  // Re-encode to UTF-8
  std::string reversed;
  reversed.reserve(word.size());
  for (size_t i = 0; i < count; i++) {
    utf8Encode(codepoints[i], reversed);
  }
  word = std::move(reversed);
}

}  // namespace ScriptDetector
