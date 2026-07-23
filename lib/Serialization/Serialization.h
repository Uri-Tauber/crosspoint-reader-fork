#pragma once
#include <HalStorage.h>
#include <Logging.h>

#include <iostream>

namespace serialization {
template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
void readPod(HalFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

// Upper bound for a single serialized string. Everything we persist through this
// header is a title, author, href or path — all far below this. The cap exists so a
// corrupt (or mis-seeked) length field cannot reach resize(): with -fno-exceptions a
// failed allocation calls abort() rather than returning, so an unvalidated 32-bit
// length turns a bad byte on the SD card into a panic.
inline constexpr uint32_t MAX_SERIALIZED_STRING = 64u * 1024u;

inline void readString(std::istream& is, std::string& s) {
  uint32_t len;
  readPod(is, len);
  if (len > MAX_SERIALIZED_STRING) {
    LOG_ERR("SERIAL", "Rejecting string length %u (max %u) — corrupt stream", len, MAX_SERIALIZED_STRING);
    s.clear();
    return;
  }
  s.resize(len);
  is.read(&s[0], len);
}

inline void readString(HalFile& file, std::string& s) {
  uint32_t len;
  readPod(file, len);
  // A string can never be longer than the bytes left in the file, so bound by both
  // the hard cap and the actual remainder — the latter also catches a stale cursor.
  const size_t pos = file.position();
  const size_t fileLen = file.size();
  const size_t remaining = fileLen > pos ? fileLen - pos : 0;
  if (len > MAX_SERIALIZED_STRING || len > remaining) {
    LOG_ERR("SERIAL", "Rejecting string length %u (max %u, %u left) — corrupt file", len, MAX_SERIALIZED_STRING,
            static_cast<uint32_t>(remaining));
    s.clear();
    return;
  }
  s.resize(len);
  file.read(&s[0], len);
}
}  // namespace serialization
