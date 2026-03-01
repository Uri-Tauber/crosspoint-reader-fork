#pragma once

#include <cstdint>

namespace ScriptDetector {

/// Check if a codepoint is a Hebrew consonant (U+05D0–U+05EA).
inline bool isHebrewCodepoint(uint32_t cp) { return cp >= 0x05D0 && cp <= 0x05EA; }

/// Check if a codepoint is RTL (Hebrew block for now; extend for Arabic later).
inline bool isRtlCodepoint(uint32_t cp) { return isHebrewCodepoint(cp); }

/// Check if text contains any Hebrew codepoints (scans entire string).
bool containsHebrew(const char* text);

/// Check the first `maxChars` Unicode codepoints of `text`.
/// Returns true if any of them is an RTL codepoint.
/// Skips combining marks (they don't count toward the limit).
bool startsWithRtl(const char* text, int maxChars = 5);

}  // namespace ScriptDetector
