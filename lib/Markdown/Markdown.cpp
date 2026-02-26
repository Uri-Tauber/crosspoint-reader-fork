#include "Markdown.h"

#include <FsHelpers.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>

Markdown::Markdown(std::string path, std::string cacheBasePath)
    : filepath(std::move(path)), cacheBasePath(std::move(cacheBasePath)) {
  const size_t hash = std::hash<std::string>{}(filepath);
  cachePath = this->cacheBasePath + "/md_" + std::to_string(hash);
}

bool Markdown::load() {
  if (loaded) {
    return true;
  }

  if (!Storage.exists(filepath.c_str())) {
    LOG_ERR("MD", "File does not exist: %s", filepath.c_str());
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("MD", filepath, file)) {
    LOG_ERR("MD", "Failed to open file: %s", filepath.c_str());
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;
  LOG_DBG("MD", "Loaded Markdown file: %s (%zu bytes)", filepath.c_str(), fileSize);
  return true;
}

std::string Markdown::getTitle() const {
  size_t lastSlash = filepath.find_last_of('/');
  std::string filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

  // Remove .md or .markdown extension
  size_t lastDot = filename.find_last_of('.');
  if (lastDot != std::string::npos) {
    filename = filename.substr(0, lastDot);
  }

  return filename;
}

void Markdown::setupCacheDir() const {
  if (!Storage.exists(cacheBasePath.c_str())) {
    Storage.mkdir(cacheBasePath.c_str());
  }
  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  }
}

std::string Markdown::findCoverImage() const {
  size_t lastSlash = filepath.find_last_of('/');
  std::string folder = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash) : "";
  if (folder.empty()) {
    folder = "/";
  }

  std::string baseName = getTitle();

  const char* extensions[] = {".bmp", ".jpg", ".jpeg", ".png", ".BMP", ".JPG", ".JPEG", ".PNG"};

  // Priority 1: image matching markdown filename
  for (const auto& ext : extensions) {
    std::string coverPath = folder + "/" + baseName + ext;
    if (Storage.exists(coverPath.c_str())) {
      LOG_DBG("MD", "Found matching cover image: %s", coverPath.c_str());
      return coverPath;
    }
  }

  // Priority 2: generic cover image
  const char* coverNames[] = {"cover", "Cover", "COVER"};
  for (const auto& name : coverNames) {
    for (const auto& ext : extensions) {
      std::string coverPath = folder + "/" + std::string(name) + ext;
      if (Storage.exists(coverPath.c_str())) {
        LOG_DBG("MD", "Found fallback cover image: %s", coverPath.c_str());
        return coverPath;
      }
    }
  }

  return "";
}

std::string Markdown::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Markdown::generateCoverBmp() const {
  if (Storage.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  std::string coverImagePath = findCoverImage();
  if (coverImagePath.empty()) {
    LOG_DBG("MD", "No cover image found for Markdown file");
    return false;
  }

  setupCacheDir();

  const size_t len = coverImagePath.length();
  const bool isJpg =
      (len >= 4 && (coverImagePath.substr(len - 4) == ".jpg" || coverImagePath.substr(len - 4) == ".JPG")) ||
      (len >= 5 && (coverImagePath.substr(len - 5) == ".jpeg" || coverImagePath.substr(len - 5) == ".JPEG"));
  const bool isBmp = len >= 4 && (coverImagePath.substr(len - 4) == ".bmp" || coverImagePath.substr(len - 4) == ".BMP");

  if (isBmp) {
    LOG_DBG("MD", "Copying BMP cover image to cache");
    FsFile src, dst;
    if (!Storage.openFileForRead("MD", coverImagePath, src)) {
      return false;
    }
    if (!Storage.openFileForWrite("MD", getCoverBmpPath(), dst)) {
      src.close();
      return false;
    }
    uint8_t buffer[1024];
    while (src.available()) {
      size_t bytesRead = src.read(buffer, sizeof(buffer));
      dst.write(buffer, bytesRead);
    }
    src.close();
    dst.close();
    LOG_DBG("MD", "Copied BMP cover to cache");
    return true;
  }

  if (isJpg) {
    LOG_DBG("MD", "Generating BMP from JPG cover image");
    FsFile coverJpg, coverBmp;
    if (!Storage.openFileForRead("MD", coverImagePath, coverJpg)) {
      return false;
    }
    if (!Storage.openFileForWrite("MD", getCoverBmpPath(), coverBmp)) {
      coverJpg.close();
      return false;
    }
    const bool success = JpegToBmpConverter::jpegFileToBmpStream(coverJpg, coverBmp);
    coverJpg.close();
    coverBmp.close();

    if (!success) {
      LOG_ERR("MD", "Failed to generate BMP from JPG cover image");
      Storage.remove(getCoverBmpPath().c_str());
    } else {
      LOG_DBG("MD", "Generated BMP from JPG cover image");
    }
    return success;
  }

  LOG_ERR("MD", "Cover image format not supported (only BMP/JPG/JPEG)");
  return false;
}

bool Markdown::readContent(uint8_t* buffer, size_t offset, size_t length) const {
  if (!loaded) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("MD", filepath, file)) {
    return false;
  }

  if (!file.seek(offset)) {
    file.close();
    return false;
  }

  size_t bytesRead = file.read(buffer, length);
  file.close();

  return bytesRead > 0;
}
