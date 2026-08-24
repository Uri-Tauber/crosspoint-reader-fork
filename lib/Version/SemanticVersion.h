#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace semantic_version {

struct Version {
  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t patch = 0;
  bool releaseCandidate = false;
};

namespace detail {

constexpr bool isAsciiDigit(const char c) { return c >= '0' && c <= '9'; }

constexpr bool isAsciiLetter(const char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

inline bool parseComponent(const std::string_view text, size_t& pos, uint32_t& value) {
  if (pos >= text.size() || !isAsciiDigit(text[pos])) return false;

  uint32_t parsed = 0;
  do {
    const uint32_t digit = static_cast<uint32_t>(text[pos] - '0');
    if (parsed > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
    parsed = parsed * 10 + digit;
    ++pos;
  } while (pos < text.size() && isAsciiDigit(text[pos]));

  value = parsed;
  return true;
}

constexpr bool startsWithRc(const std::string_view suffix) {
  return suffix.size() >= 2 && (suffix[0] == 'r' || suffix[0] == 'R') && (suffix[1] == 'c' || suffix[1] == 'C');
}

}  // namespace detail

// Parses the numeric core used by CrossPoint release tags and build versions.
// An optional leading 'v' and suffixes such as "-rc+hash", "rc", and
// "-dev-branch-sha" are accepted; a fourth numeric component is not.
inline bool parse(std::string_view text, Version& version) {
  size_t pos = 0;
  if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) ++pos;

  Version parsed;
  if (!detail::parseComponent(text, pos, parsed.major) || pos >= text.size() || text[pos++] != '.' ||
      !detail::parseComponent(text, pos, parsed.minor) || pos >= text.size() || text[pos++] != '.' ||
      !detail::parseComponent(text, pos, parsed.patch)) {
    return false;
  }

  std::string_view suffix = text.substr(pos);
  if (!suffix.empty()) {
    const char first = suffix.front();
    if (first != '-' && first != '+' && !detail::isAsciiLetter(first)) return false;

    if (first == '-') {
      suffix.remove_prefix(1);
      parsed.releaseCandidate = detail::startsWithRc(suffix);
    } else if (detail::isAsciiLetter(first)) {
      parsed.releaseCandidate = detail::startsWithRc(suffix);
    }
  }

  version = parsed;
  return true;
}

inline bool isNewer(const Version& candidate, const Version& current) {
  if (candidate.major != current.major) return candidate.major > current.major;
  if (candidate.minor != current.minor) return candidate.minor > current.minor;
  if (candidate.patch != current.patch) return candidate.patch > current.patch;

  return current.releaseCandidate && !candidate.releaseCandidate;
}

}  // namespace semantic_version
