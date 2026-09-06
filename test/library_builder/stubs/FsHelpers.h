#pragma once
#include <string>
namespace FsHelpers {
inline bool checkFileExtension(const std::string& path, const char* ext) { return path.ends_with(ext); }
inline bool hasEpubExtension(const std::string& path) { return checkFileExtension(path, ".epub"); }
}  // namespace FsHelpers
