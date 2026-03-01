#include "ScriptDetector.h"

#include <Utf8.h>

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

}  // namespace ScriptDetector
