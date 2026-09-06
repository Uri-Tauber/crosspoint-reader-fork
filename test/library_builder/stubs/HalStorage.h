#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fake {
struct Node {
  bool directory = false;
  uint32_t time = 1;
  std::vector<uint8_t> bytes;
};
inline std::map<std::string, std::shared_ptr<Node>> files;
inline int failRead = -1, failWrite = -1, failRename = -1, failAlloc = -1;
inline int failClose = -1;
inline unsigned walks = 0, parses = 0;
inline uint64_t reads = 0, writes = 0;
inline bool failureTriggered = false;
inline bool fail(int& count) {
  if (count < 0) return false;
  if (!count) {
    count = -1;
    failureTriggered = true;
    return true;
  }
  --count;
  return false;
}
inline void reset() {
  files.clear();
  failClose = -1;
  failRead = failWrite = failRename = failAlloc = -1;
  walks = parses = 0;
  reads = writes = 0;
}
inline void add(const std::string& path, const std::string& bytes = "book", uint32_t time = 1) {
  auto node = std::make_shared<Node>();
  node->time = time;
  node->bytes.assign(bytes.begin(), bytes.end());
  files[path] = node;
  auto parent = path.substr(0, path.find_last_of('/'));
  if (parent.empty()) parent = "/";
  if (!files.count(parent)) {
    add(parent, "");
    files[parent]->directory = true;
  }
}
}  // namespace fake
class HalFile {
 public:
  std::shared_ptr<fake::Node> node;
  std::string path;
  size_t pos = 0;
  explicit operator bool() const { return bool(node); }
  bool isOpen() const { return bool(node); }
  bool close() {
    const bool failed = fake::fail(fake::failClose);
    node.reset();
    return !failed;
  }
  bool isDirectory() const { return node && node->directory; }
  void rewindDirectory() { pos = 0; }
  HalFile openNextFile() {
    ++fake::walks;
    std::vector<std::string> children;
    for (auto& [name, value] : fake::files) {
      auto parent = name.substr(0, name.find_last_of('/'));
      if (parent.empty()) parent = "/";
      if (name != path && parent == path) children.push_back(name);
    }
    if (pos >= children.size()) return {};
    HalFile file;
    file.path = children[pos++];
    file.node = fake::files[file.path];
    return file;
  }
  size_t getName(char* out, size_t size) {
    auto name = path.substr(path.find_last_of('/') + 1);
    strncpy(out, name.c_str(), size);
    return name.size();
  }
  struct IoCounts {
    uint64_t readBytes, writtenBytes;
  };
  static IoCounts ioCounts() { return {fake::reads, fake::writes}; }
  uint32_t modificationTime() { return node->time; }
  uint64_t fileSize64() const { return node ? node->bytes.size() : 0; }
  size_t fileSize() const { return fileSize64(); }
  size_t position() const { return pos; }
  bool seekSet(size_t offset) {
    if (!node) return false;
    pos = offset;
    return true;
  }
  int read(void* out, size_t size) {
    if (!size) return 0;
    if (!node || fake::fail(fake::failRead)) return -1;
    size = std::min(size, node->bytes.size() - std::min(pos, node->bytes.size()));
    memcpy(out, node->bytes.data() + std::min(pos, node->bytes.size()), size);
    pos += size;
    fake::reads += size;
    return size;
  }
  size_t write(const uint8_t* data, size_t size) {
    if (!size) return 0;
    if (!node || fake::fail(fake::failWrite)) return 0;
    node->bytes.resize(std::max(node->bytes.size(), pos + size));
    memcpy(node->bytes.data() + pos, data, size);
    pos += size;
    fake::writes += size;
    return size;
  }
};
class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  bool exists(const char* path) { return fake::files.count(path); }
  bool mkdir(const char* path) {
    if (!exists(path)) fake::add(path, "");
    fake::files[path]->directory = true;
    return true;
  }
  HalFile open(const char* path) {
    HalFile file;
    auto found = fake::files.find(path);
    if (found != fake::files.end()) {
      file.node = found->second;
      file.path = path;
    }
    return file;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file = open(path);
    return bool(file);
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    fake::add(path, "");
    file = open(path);
    return true;
  }
  bool openFileForWrite(const char* mod, const std::string& path, HalFile& file) {
    return openFileForWrite(mod, path.c_str(), file);
  }
  bool remove(const char* path) { return fake::files.erase(path); }
  bool rename(const char* from, const char* to) {
    if (fake::fail(fake::failRename) || !exists(from) || exists(to)) return false;
    fake::files[to] = fake::files[from];
    fake::files.erase(from);
    return true;
  }
};
#define Storage HalStorage::getInstance()
