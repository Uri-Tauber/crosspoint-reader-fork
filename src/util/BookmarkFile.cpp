#include "BookmarkFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include "BookmarkUtil.h"

bool BookmarkFile::load(const std::string& bookPath, std::vector<BookmarkEntry>& bookmarks) {
  bookmarks.clear();

  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  if (!Storage.exists(path.c_str())) {
    LOG_DBG("BKM", "No bookmark file found at %s", path.c_str());
    return false;
  }
  String json = Storage.readFile(path.c_str());
  if (json.isEmpty()) {
    LOG_ERR("BKM", "Failed to load bookmarks from %s (empty)", path.c_str());
    return false;
  }

  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("BKM", "JSON parse error: %s", error.c_str());
    return false;
  }

  JsonArray arr = doc["bookmarks"].as<JsonArray>();
  bookmarks.reserve(arr.size());
  for (JsonObject obj : arr) {
    bookmarks.emplace_back();
    auto& bookmark = bookmarks.back();
    bookmark.xpath = obj["xpath"] | "";
    bookmark.percentage = obj["percentage"] | static_cast<float>(0);
    bookmark.summary = obj["summary"] | "";
    bookmark.computedSpineIndex = obj["si"] | static_cast<uint16_t>(0);
    bookmark.computedChapterPageCount = obj["pc"] | static_cast<uint16_t>(0);
    bookmark.computedChapterProgress = obj["pp"] | static_cast<uint16_t>(0);
  }

  LOG_DBG("BKM", "Loaded %zu bookmarks from file", bookmarks.size());
  return true;
}

bool BookmarkFile::save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks) {
  JsonDocument doc;
  JsonArray arr = doc["bookmarks"].to<JsonArray>();
  LOG_DBG("BKM", "Saving %zu bookmarks to file", bookmarks.size());
  for (const auto& bookmark : bookmarks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["xpath"] = bookmark.xpath;
    obj["percentage"] = bookmark.percentage;
    obj["summary"] = bookmark.summary;
    obj["si"] = bookmark.computedSpineIndex;
    obj["pc"] = bookmark.computedChapterPageCount;
    obj["pp"] = bookmark.computedChapterProgress;
  }

  Storage.mkdir(BookmarkUtil::getBookmarksDir().c_str());
  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  String json;
  serializeJson(doc, json);
  if (!Storage.writeFile(path.c_str(), json)) {
    LOG_ERR("BKM", "Failed to save bookmarks to: %s", path.c_str());
    return false;
  }
  return true;
}
