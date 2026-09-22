#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class HalFile;

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool openFileForRead(const char*, const char* path, HalFile& file);
  bool openFileForWrite(const char*, const char* path, HalFile& file);
  bool remove(const char* path) { return files.erase(path) > 0; }
  bool rename(const char* oldPath, const char* newPath) {
    const auto it = files.find(oldPath);
    if (it == files.end() || files.contains(newPath)) return false;
    files.emplace(newPath, std::move(it->second));
    files.erase(it);
    return true;
  }

  void reset() { files.clear(); }
  bool hasFile(const std::string& path) const { return files.contains(path); }
  const std::vector<uint8_t>& file(const std::string& path) const { return files.at(path); }
  void setFile(const std::string& path, std::vector<uint8_t> bytes) { files[path] = std::move(bytes); }

 private:
  std::unordered_map<std::string, std::vector<uint8_t>> files;
};

#define Storage HalStorage::getInstance()

class HalFile {
  friend class HalStorage;

 public:
  HalFile() = default;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush() {}
  uint64_t fileSize64() const { return data ? data->size() : 0; }
  bool seekSet(const size_t newPosition) {
    if (!open || !data || newPosition > data->size()) return false;
    position = newPosition;
    return true;
  }
  int read(void* dst, const size_t count) {
    if (!open || !data) return 0;
    const size_t available = data->size() - position;
    const size_t readCount = count < available ? count : available;
    std::memcpy(dst, data->data() + position, readCount);
    position += readCount;
    return static_cast<int>(readCount);
  }
  size_t write(const void* src, const size_t count) {
    if (!open || !writable || !data) return 0;
    if (position + count > data->size()) data->resize(position + count);
    std::memcpy(data->data() + position, src, count);
    position += count;
    return count;
  }
  bool close() {
    open = false;
    return true;
  }
  bool isOpen() const { return open; }

 private:
  std::vector<uint8_t>* data = nullptr;
  size_t position = 0;
  bool open = false;
  bool writable = false;
};

inline bool HalStorage::openFileForRead(const char*, const char* path, HalFile& file) {
  const auto it = files.find(path);
  file.data = it == files.end() ? nullptr : &it->second;
  file.position = 0;
  file.open = file.data != nullptr;
  file.writable = false;
  return file.open;
}

inline bool HalStorage::openFileForWrite(const char*, const char* path, HalFile& file) {
  auto& bytes = files[path];
  bytes.clear();
  file.data = &bytes;
  file.position = 0;
  file.open = true;
  file.writable = true;
  return true;
}
