#pragma once
#include "Epub/LibraryMetadata.h"
#include "HalStorage.h"
struct FakeMetadata {
  std::string title = "Title", author = "Author", date, publisher, language, series = "Series", position = "1", subject;
  bool success = true, calibre = false;
};
inline std::map<std::string, FakeMetadata> bookMetadata;
class Epub {
  std::string path;

 public:
  Epub(const std::string& path, const char*) : path(path) {}
  bool loadMetadata(std::string& title, std::string& author, LibraryMetadata* out) {
    ++fake::parses;
    const auto& meta = bookMetadata[path];
    if (!meta.success) return false;
    title = meta.title;
    author = meta.author;
    out->reset();
    strncpy(out->values[LibraryMetadata::Date], meta.date.c_str(), LibraryMetadata::TEXT_BYTES - 1);
    strncpy(out->values[LibraryMetadata::Publisher], meta.publisher.c_str(), LibraryMetadata::TEXT_BYTES - 1);
    strncpy(out->values[LibraryMetadata::Language], meta.language.c_str(), LibraryMetadata::TEXT_BYTES - 1);
    strncpy(out->values[LibraryMetadata::Series], meta.series.c_str(), LibraryMetadata::TEXT_BYTES - 1);
    strncpy(out->values[LibraryMetadata::Subject], meta.subject.c_str(), LibraryMetadata::TEXT_BYTES - 1);
    strncpy(out->seriesIndex, meta.position.c_str(), sizeof(out->seriesIndex) - 1);
    out->calibreIndex = meta.calibre;
    return true;
  }
};
