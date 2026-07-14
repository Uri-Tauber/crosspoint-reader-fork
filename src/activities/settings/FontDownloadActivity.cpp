#include "FontDownloadActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

FontDownloadActivity::FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("FontDownload", renderer, mappedInput), fontInstaller_(sdFontSystem.registry()) {}

// --- Lifecycle ---

void FontDownloadActivity::onEnter() {
  Activity::onEnter();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void FontDownloadActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void FontDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }

  {
    RenderLock lock(*this);
    state_ = LOADING_MANIFEST;
  }
  requestUpdateAndWait();

  if (!fetchAndParseManifest()) {
    {
      RenderLock lock(*this);
      state_ = ERROR;
    }
    return;
  }

  {
    RenderLock lock(*this);
    if (hasGroupScreen()) {
      state_ = GROUP_LIST;
      selectedGroupIndex_ = 0;
    } else {
      // No script grouping in the manifest (older manifest or no coverage
      // data): skip the group screen and show every family directly.
      buildFilteredIndices(0);
      state_ = FAMILY_LIST;
      selectedIndex_ = 0;
    }
  }
}

// --- Manifest fetching ---

bool FontDownloadActivity::fetchAndParseManifest() {
  // Download manifest to a temp file on SD card to avoid holding both
  // TLS buffers and the full JSON string in RAM simultaneously.
  static constexpr const char* MANIFEST_TMP = "/fonts_manifest.tmp";

  auto result = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, MANIFEST_TMP, nullptr);
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT", "Failed to fetch manifest from %s", FONT_MANIFEST_URL);
    errorMessage_ = "Failed to fetch font list";
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  // HTTP client is now closed — TLS buffers freed. Parse JSON from file.
  HalFile manifestFile;
  if (!Storage.openFileForRead("FONT", MANIFEST_TMP, manifestFile)) {
    LOG_ERR("FONT", "Failed to open temp manifest");
    Storage.remove(MANIFEST_TMP);
    errorMessage_ = "Failed to read font list";
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, manifestFile);
  manifestFile.close();
  Storage.remove(MANIFEST_TMP);

  if (err) {
    LOG_ERR("FONT", "Manifest parse error: %s", err.c_str());
    errorMessage_ = "Invalid font manifest";
    return false;
  }

  int version = doc["version"] | 0;
  if (version != FONTS_MANIFEST_VERSION) {
    LOG_ERR("FONT", "Unsupported manifest version: %d", version);
    errorMessage_ = "Unsupported manifest version";
    return false;
  }

  baseUrl_ = doc["baseUrl"] | "";
  families_.clear();
  fontInstaller_.refreshRegistry();

  // Parse the top-level script-group table first so each family's `scripts`
  // tags can be resolved to bits below. Capped at 32 groups (scriptMask width).
  scriptGroups_.clear();
  JsonArray groupsArr = doc["scriptGroups"].as<JsonArray>();
  scriptGroups_.reserve(groupsArr.size());
  for (JsonObject gObj : groupsArr) {
    if (scriptGroups_.size() >= 32) break;
    ScriptGroup group;
    group.tag = gObj["tag"] | "";
    group.label = gObj["label"] | "";
    if (!group.tag.empty()) scriptGroups_.push_back(std::move(group));
  }

  JsonArray familiesArr = doc["families"].as<JsonArray>();
  families_.reserve(familiesArr.size());

  for (JsonObject fObj : familiesArr) {
    ManifestFamily family;
    family.name = fObj["name"] | "";
    family.description = fObj["description"] | "";

    for (JsonVariant s : fObj["styles"].as<JsonArray>()) {
      family.styles.push_back(s.as<std::string>());
    }

    // Resolve the family's script tags to bits over scriptGroups_. Unknown tags
    // (not present in scriptGroups_) are ignored — the family simply won't show
    // under a missing group, but always appears under "All fonts".
    family.scriptMask = 0;
    for (JsonVariant s : fObj["scripts"].as<JsonArray>()) {
      const char* tag = s.as<const char*>();
      if (!tag) continue;
      for (size_t gi = 0; gi < scriptGroups_.size(); gi++) {
        if (scriptGroups_[gi].tag == tag) {
          family.scriptMask |= (1u << gi);
          break;
        }
      }
    }

    family.totalSize = 0;
    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      ManifestFile file;
      file.name = fileObj["name"] | "";
      file.size = fileObj["size"] | 0;

      if (!fileObj["crc32"].is<uint32_t>()) {
        LOG_ERR("FONT", "Malformed manifest file entry: missing or invalid crc32 for %s", file.name.c_str());
        errorMessage_ = "Invalid font manifest";
        return false;
      }
      file.crc32 = fileObj["crc32"].as<uint32_t>();

      family.totalSize += file.size;
      family.files.push_back(std::move(file));
    }

    family.installed = fontInstaller_.isFamilyInstalled(family.name.c_str());

    // Detect updates by comparing manifest file sizes with files on disk.
    // Not a checksum, but a size mismatch reliably indicates a rebuild in practice.
    if (family.installed) {
      for (const auto& file : family.files) {
        char path[128];
        FontInstaller::buildFontPath(family.name.c_str(), file.name.c_str(), path, sizeof(path));
        HalFile f;
        if (Storage.openFileForRead("FONT", path, f)) {
          size_t actual = f.fileSize();
          f.close();
          if (actual != file.size) {
            family.hasUpdate = true;
            break;
          }
        } else {
          // File missing on disk but family dir exists — treat as update
          family.hasUpdate = true;
          break;
        }
      }
    }

    families_.push_back(std::move(family));
  }

  LOG_DBG("FONT", "Manifest loaded: %zu families", families_.size());
  return true;
}

// --- Download ---

void FontDownloadActivity::downloadAll() {
  cancelRequested_ = false;
  for (int idx : filteredIndices_) {
    if (families_[idx].installed) continue;
    downloadFamily(families_[idx]);
    if (state_ == ERROR || cancelRequested_) return;
  }

  {
    RenderLock lock(*this);
    state_ = COMPLETE;
  }
}

void FontDownloadActivity::updateAll() {
  cancelRequested_ = false;
  for (int idx : filteredIndices_) {
    if (!families_[idx].hasUpdate) continue;
    downloadFamily(families_[idx]);
    if (state_ == ERROR || cancelRequested_) return;
  }

  {
    RenderLock lock(*this);
    state_ = COMPLETE;
  }
}

bool FontDownloadActivity::showDownloadAllRow() const {
  for (int idx : filteredIndices_) {
    if (!families_[idx].installed) return true;
  }
  return false;
}

bool FontDownloadActivity::showUpdateAllRow() const {
  for (int idx : filteredIndices_) {
    if (families_[idx].hasUpdate) return true;
  }
  return false;
}

int FontDownloadActivity::specialRowCount() const {
  return (showDownloadAllRow() ? 1 : 0) + (showUpdateAllRow() ? 1 : 0);
}

bool FontDownloadActivity::isDownloadAllRow(int index) const { return showDownloadAllRow() && index == 0; }

bool FontDownloadActivity::isUpdateAllRow(int index) const {
  return showUpdateAllRow() && index == (showDownloadAllRow() ? 1 : 0);
}

int FontDownloadActivity::listItemCount() const {
  return filteredIndices_.empty() ? 0 : static_cast<int>(filteredIndices_.size()) + specialRowCount();
}

int FontDownloadActivity::familyIndexFromList(int listIndex) const {
  const int pos = listIndex - specialRowCount();
  if (pos < 0 || pos >= static_cast<int>(filteredIndices_.size())) return -1;
  return filteredIndices_[pos];
}

int FontDownloadActivity::groupMemberCount(int scriptGroupIndex) const {
  if (scriptGroupIndex < 0 || scriptGroupIndex >= static_cast<int>(scriptGroups_.size())) return 0;
  const uint32_t bit = 1u << scriptGroupIndex;
  int count = 0;
  for (const auto& f : families_) {
    if (f.scriptMask & bit) count++;
  }
  return count;
}

void FontDownloadActivity::buildFilteredIndices(int groupListIndex) {
  filteredIndices_.clear();
  filteredIndices_.reserve(families_.size());
  if (groupListIndex <= 0) {
    // "All fonts": every family, in manifest order.
    for (int i = 0; i < static_cast<int>(families_.size()); i++) filteredIndices_.push_back(i);
    return;
  }
  const uint32_t bit = 1u << (groupListIndex - 1);
  for (int i = 0; i < static_cast<int>(families_.size()); i++) {
    if (families_[i].scriptMask & bit) filteredIndices_.push_back(i);
  }
}

void FontDownloadActivity::enterGroup(int groupListIndex) {
  buildFilteredIndices(groupListIndex);
  RenderLock lock(*this);
  selectedGroupIndex_ = groupListIndex;
  selectedIndex_ = 0;
  state_ = FAMILY_LIST;
}

size_t FontDownloadActivity::totalDownloadSize() const {
  size_t total = 0;
  for (int idx : filteredIndices_) {
    if (!families_[idx].installed) total += families_[idx].totalSize;
  }
  return total;
}

size_t FontDownloadActivity::totalUpdateSize() const {
  size_t total = 0;
  for (int idx : filteredIndices_) {
    if (families_[idx].hasUpdate) total += families_[idx].totalSize;
  }
  return total;
}

// Standard CRC32 matching zlib/Python zlib.crc32().
bool FontDownloadActivity::computeFileCrc32(const char* path, uint32_t& outCrc) {
  HalFile f;
  if (!Storage.openFileForRead("FONT", path, f)) {
    return false;
  }
  constexpr size_t BUF_SIZE = 128;
  uint8_t buf[BUF_SIZE];
  uint32_t crc = 0;
  while (f.available()) {
    const int n = f.read(buf, BUF_SIZE);
    if (n <= 0) break;
    crc = esp_rom_crc32_le(crc, buf, static_cast<uint32_t>(n));
  }
  outCrc = crc;
  return true;
}

void FontDownloadActivity::downloadFamily(ManifestFamily& family) {
  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloadingFamilyIndex_ = static_cast<int>(&family - families_.data());
    fileProgress_ = 0;
    fileTotal_ = 0;
    cancelRequested_ = false;
  }
  requestUpdateAndWait();

  if (!fontInstaller_.ensureFamilyDir(family.name.c_str())) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = "Failed to create font directory";
    return;
  }

  for (size_t i = 0; i < family.files.size(); i++) {
    const auto& file = family.files[i];

    {
      RenderLock lock(*this);
      fileProgress_ = 0;
      fileTotal_ = file.size;
    }
    requestUpdateAndWait();

    char destPath[128];
    FontInstaller::buildFontPath(family.name.c_str(), file.name.c_str(), destPath, sizeof(destPath));

    std::string url = baseUrl_ + file.name;

    auto result = HttpDownloader::downloadToFile(
        url, destPath,
        [this](size_t downloaded, size_t total) {
          fileProgress_ = downloaded;
          fileTotal_ = total;
          mappedInput.update();
          if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
              mappedInput.wasPressed(MappedInputManager::Button::Back)) {
            cancelRequested_ = true;
          }
          requestUpdate(true);
        },
        &cancelRequested_);

    if (result == HttpDownloader::ABORTED) {
      fontInstaller_.deleteFamily(family.name.c_str());
      family.installed = false;
      family.hasUpdate = false;
      {
        RenderLock lock(*this);
        state_ = FAMILY_LIST;
      }
      return;
    }

    if (result != HttpDownloader::OK) {
      LOG_ERR("FONT", "Download failed: %s (%d)", file.name.c_str(), result);
      fontInstaller_.deleteFamily(family.name.c_str());
      family.installed = false;
      family.hasUpdate = false;
      RenderLock lock(*this);
      state_ = ERROR;
      errorMessage_ = "Download failed: " + file.name;
      return;
    }

    uint32_t actualCrc = 0;
    if (!computeFileCrc32(destPath, actualCrc)) {
      LOG_ERR("FONT", "Failed to open file for CRC check: %s", destPath);
      fontInstaller_.deleteFamily(family.name.c_str());
      family.installed = false;
      family.hasUpdate = false;
      RenderLock lock(*this);
      state_ = ERROR;
      errorMessage_ = "Failed to compute checksum: " + file.name;
      return;
    }
    if (actualCrc != file.crc32) {
      LOG_ERR("FONT", "CRC32 mismatch for %s: got %08x expected %08x", file.name.c_str(), actualCrc, file.crc32);
      fontInstaller_.deleteFamily(family.name.c_str());
      family.installed = false;
      family.hasUpdate = false;
      RenderLock lock(*this);
      state_ = ERROR;
      errorMessage_ = "Checksum mismatch: " + file.name;
      return;
    }
    LOG_DBG("FONT", "Downloaded %s (size=%zu crc32=%08x)", file.name.c_str(), file.size, actualCrc);

    if (!fontInstaller_.validateCpfontFile(destPath)) {
      LOG_ERR("FONT", "Invalid .cpfont: %s", destPath);
      fontInstaller_.deleteFamily(family.name.c_str());
      family.installed = false;
      family.hasUpdate = false;
      RenderLock lock(*this);
      state_ = ERROR;
      errorMessage_ = "Invalid font file: " + file.name;
      return;
    }
    currentFileIndex_++;
  }

  fontInstaller_.refreshRegistry();
  family.installed = true;
  family.hasUpdate = false;

  {
    RenderLock lock(*this);
    state_ = COMPLETE;
  }
}

void FontDownloadActivity::promptDeleteSelectedFamily() {
  const int pendingDeleteFamilyIndex = familyIndexFromList(selectedIndex_);
  if (pendingDeleteFamilyIndex < 0 || pendingDeleteFamilyIndex >= static_cast<int>(families_.size())) {
    return;
  }

  std::string heading = tr(STR_DELETE);
  const auto& family = families_[pendingDeleteFamilyIndex];
  std::string body = family.name;
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, body),
                         [this](const ActivityResult& result) { onDeleteConfirmationResult(result); });
}

void FontDownloadActivity::onDeleteConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    requestUpdate();
    return;
  }

  auto& family = families_[familyIndexFromList(selectedIndex_)];

  if (fontInstaller_.deleteFamily(family.name.c_str()) != FontInstaller::Error::OK) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = "Failed to delete font";
  } else {
    fontInstaller_.refreshRegistry();
    family.installed = false;
    family.hasUpdate = false;
  }

  requestUpdate();
}

bool FontDownloadActivity::isSelectedFamilyDeletable() const {
  if (isDownloadAllRow(selectedIndex_) || isUpdateAllRow(selectedIndex_)) return false;
  if (selectedIndex_ < specialRowCount() || selectedIndex_ >= listItemCount()) return false;
  const auto& family = families_[familyIndexFromList(selectedIndex_)];
  return family.installed && !family.hasUpdate;
}

// --- Input handling ---

void FontDownloadActivity::loop() {
  if (state_ == GROUP_LIST) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    const int listSize = groupListItemCount();
    const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

    buttonNavigator_.onNextRelease([this, listSize] {
      selectedGroupIndex_ = ButtonNavigator::nextIndex(selectedGroupIndex_, listSize);
      requestUpdate();
    });
    buttonNavigator_.onPreviousRelease([this, listSize] {
      selectedGroupIndex_ = ButtonNavigator::previousIndex(selectedGroupIndex_, listSize);
      requestUpdate();
    });
    buttonNavigator_.onNextContinuous([this, listSize, pageItems] {
      selectedGroupIndex_ = ButtonNavigator::nextPageIndex(selectedGroupIndex_, listSize, pageItems);
      requestUpdate();
    });
    buttonNavigator_.onPreviousContinuous([this, listSize, pageItems] {
      selectedGroupIndex_ = ButtonNavigator::previousPageIndex(selectedGroupIndex_, listSize, pageItems);
      requestUpdate();
    });

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      enterGroup(selectedGroupIndex_);
      requestUpdate();
      return;
    }
  } else if (state_ == FAMILY_LIST) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      if (hasGroupScreen()) {
        RenderLock lock(*this);
        state_ = GROUP_LIST;
      } else {
        finish();
        return;
      }
      requestUpdate();
      return;
    }

    const int listSize = listItemCount();
    const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

    buttonNavigator_.onNextRelease([this, listSize] {
      selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
      requestUpdate();
    });

    buttonNavigator_.onPreviousRelease([this, listSize] {
      selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
      requestUpdate();
    });

    buttonNavigator_.onNextContinuous([this, listSize, pageItems] {
      selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, listSize, pageItems);
      requestUpdate();
    });

    buttonNavigator_.onPreviousContinuous([this, listSize, pageItems] {
      selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, listSize, pageItems);
      requestUpdate();
    });

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (!filteredIndices_.empty()) {
        if (isDownloadAllRow(selectedIndex_)) {
          currentFileIndex_ = 0;
          currentFileTotal_ = 0;
          for (int idx : filteredIndices_) {
            if (!families_[idx].installed) currentFileTotal_ += families_[idx].files.size();
          }

          downloadAll();
        } else if (isUpdateAllRow(selectedIndex_)) {
          currentFileIndex_ = 0;
          currentFileTotal_ = 0;
          for (int idx : filteredIndices_) {
            if (families_[idx].hasUpdate) currentFileTotal_ += families_[idx].files.size();
          }
          updateAll();
        } else {
          auto& family = families_[familyIndexFromList(selectedIndex_)];
          if (!family.installed || family.hasUpdate) {
            currentFileIndex_ = 0;
            currentFileTotal_ = family.files.size();
            downloadFamily(family);
          } else {
            promptDeleteSelectedFamily();
            return;
          }
        }
        requestUpdateAndWait();
        return;
      }
    }
  } else if (state_ == COMPLETE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      {
        RenderLock lock(*this);
        state_ = FAMILY_LIST;
      }
      requestUpdate();
    }
  } else if (state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      {
        RenderLock lock(*this);
        state_ = FAMILY_LIST;
      }
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (downloadingFamilyIndex_ >= 0 && downloadingFamilyIndex_ < static_cast<int>(families_.size())) {
        downloadFamily(families_[downloadingFamilyIndex_]);
        requestUpdateAndWait();
        return;
      } else {
        {
          RenderLock lock(*this);
          state_ = FAMILY_LIST;
        }
        requestUpdate();
      }
    }
  }
}

// --- Rendering ---

std::string FontDownloadActivity::formatSize(size_t bytes) {
  char buf[32];
  if (bytes >= 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%zu B", bytes);
  }
  return buf;
}

void FontDownloadActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Within a script group, show that group's name as the header subtitle.
  std::string headerSubtitle;
  if (state_ == FAMILY_LIST && hasGroupScreen()) {
    headerSubtitle = (selectedGroupIndex_ >= 1 && selectedGroupIndex_ <= static_cast<int>(scriptGroups_.size()))
                         ? scriptGroups_[selectedGroupIndex_ - 1].label
                         : std::string(tr(STR_ALL_FONTS));
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FONT_BROWSER),
                 headerSubtitle.empty() ? nullptr : headerSubtitle.c_str());

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto centerY = (pageHeight - lineHeight) / 2;

  if (state_ == LOADING_MANIFEST) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_LOADING_FONT_LIST));
  } else if (state_ == GROUP_LIST) {
    const Rect listRect{0, contentTop, pageWidth,
                        pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing};
    GUI.drawList(
        renderer, listRect, groupListItemCount(), selectedGroupIndex_,
        [this](int index) -> std::string {
          if (index == 0) return tr(STR_ALL_FONTS);
          return scriptGroups_[index - 1].label;
        },
        nullptr, nullptr,
        [this](int index) -> std::string {
          const int count = (index == 0) ? static_cast<int>(families_.size()) : groupMemberCount(index - 1);
          return std::to_string(count);
        },
        false, nullptr);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == FAMILY_LIST) {
    if (families_.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_NO_FONTS_AVAILABLE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
      GUI.drawList(
          renderer,
          Rect{0, contentTop, pageWidth, pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing},
          listItemCount(), selectedIndex_,
          [this](int index) -> std::string {
            if (isDownloadAllRow(index)) {
              return std::string(tr(STR_DOWNLOAD_ALL)) + " (" + formatSize(totalDownloadSize()) + ")";
            }
            if (isUpdateAllRow(index)) {
              return std::string(tr(STR_UPDATE_ALL)) + " (" + formatSize(totalUpdateSize()) + ")";
            }
            return families_[familyIndexFromList(index)].name;
          },
          [this](int index) -> std::string {
            if (isDownloadAllRow(index) || isUpdateAllRow(index)) return "";
            return families_[familyIndexFromList(index)].description;
          },
          nullptr,
          [this](int index) -> std::string {
            if (isDownloadAllRow(index) || isUpdateAllRow(index)) return "";
            const auto& f = families_[familyIndexFromList(index)];
            if (f.hasUpdate) return tr(STR_UPDATE_AVAILABLE);
            if (f.installed) return tr(STR_INSTALLED);
            return "";
          },
          true,
          [this](int index) -> bool {
            if (isDownloadAllRow(index) || isUpdateAllRow(index)) return false;
            const auto& f = families_[familyIndexFromList(index)];
            return f.installed && !f.hasUpdate;
          });

      const auto labels = mappedInput.mapLabels(tr(STR_BACK),
                                                isSelectedFamilyDeletable()      ? tr(STR_DELETE)
                                                : isUpdateAllRow(selectedIndex_) ? tr(STR_UPDATE)
                                                                                 : tr(STR_DOWNLOAD),
                                                tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else if (state_ == DOWNLOADING) {
    const auto& family = families_[downloadingFamilyIndex_];

    std::string statusText = std::string(tr(STR_DOWNLOADING)) + " " + family.name + " (" +
                             std::to_string(currentFileIndex_ + 1) + "/" + std::to_string(currentFileTotal_) + ")";
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, statusText.c_str());

    float progress = 0;
    if (fileTotal_ > 0) {
      progress = static_cast<float>(fileProgress_) / static_cast<float>(fileTotal_);
    }

    int barY = centerY + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, barY, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(progress * 100), 100);

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_FONT_INSTALLED), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_FONT_INSTALL_FAILED), true,
                              EpdFontFamily::BOLD);
    if (!errorMessage_.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY + metrics.verticalSpacing, errorMessage_.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
