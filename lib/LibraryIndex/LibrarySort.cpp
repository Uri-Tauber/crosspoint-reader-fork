#include "LibrarySort.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace library {
namespace {
bool decimal(const char* text, double& value) {
  if (!*text) return false;
  char* end = nullptr;
  value = strtod(text, &end);
  return end != text && !*end && std::isfinite(value);
}
bool dotted(const char* text) {
  bool digit = false;
  for (; *text; ++text) {
    if (*text >= '0' && *text <= '9')
      digit = true;
    else if (*text == '.' && digit)
      digit = false;
    else
      return false;
  }
  return digit;
}
}  // namespace
SeriesPositionKey seriesPositionKey(const char* value, bool calibre) {
  SeriesPositionKey key;
  key.calibre = calibre;
  key.valid = calibre ? decimal(value, key.decimal) : dotted(value);
  if (!key.valid) return key;
  key.major = std::floor(strtod(value, nullptr));
  const char* part = strchr(value, '.');
  key.subdivision = calibre ? key.decimal != key.major : part && strspn(part, ".0") != strlen(part);
  if (!calibre) {
    strncpy(key.dotted, value, sizeof(key.dotted) - 1);
  }
  return key;
}
int compareSeriesKeys(const SeriesPositionKey& x, const SeriesPositionKey& y) {
  if (x.valid != y.valid) return x.valid ? -1 : 1;
  if (!x.valid) return 0;
  if (x.major != y.major) return x.major < y.major ? -1 : 1;
  if (x.subdivision != y.subdivision) return x.subdivision ? 1 : -1;
  if (!x.subdivision) return 0;
  if (x.calibre != y.calibre) return x.calibre ? 1 : -1;
  if (x.calibre) return x.decimal < y.decimal ? -1 : x.decimal > y.decimal ? 1 : 0;
  const char* a = x.dotted;
  const char* b = y.dotted;
  while (*a || *b) {
    while (*a == '0') ++a;
    while (*b == '0') ++b;
    const size_t na = strcspn(a, "."), nb = strcspn(b, ".");
    if (na != nb) return na < nb ? -1 : 1;
    const int cmp = strncmp(a, b, na);
    if (cmp) return cmp;
    a += na;
    b += nb;
    if (*a == '.') ++a;
    if (*b == '.') ++b;
  }
  return 0;
}
int compareSeriesPosition(const char* a, bool calibreA, const char* b, bool calibreB) {
  return compareSeriesKeys(seriesPositionKey(a, calibreA), seriesPositionKey(b, calibreB));
}

uint32_t publicationDateKey(const char* value) {
  const size_t len = strlen(value);
  if (len != 4 && len != 7 && len < 10) return 0;
  unsigned year = 0, month = 0, day = 0;
  for (int i = 0; i < 4; ++i) {
    if (value[i] < '0' || value[i] > '9') return 0;
    year = year * 10 + value[i] - '0';
  }
  if (!year) return 0;
  if (len >= 7) {
    if (value[4] != '-' || value[5] < '0' || value[5] > '9' || value[6] < '0' || value[6] > '9') return 0;
    month = (value[5] - '0') * 10 + value[6] - '0';
    if (month < 1 || month > 12) return 0;
  }
  if (len >= 10) {
    if (value[7] != '-' || value[8] < '0' || value[8] > '9' || value[9] < '0' || value[9] > '9') return 0;
    day = (value[8] - '0') * 10 + value[9] - '0';
    static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const unsigned maxDay = DAYS[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (day < 1 || day > maxDay || (len > 10 && value[10] != 'T')) return 0;
  }
  return year * 10000 + month * 100 + day;
}
}  // namespace library
