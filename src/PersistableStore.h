#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <ObfuscationUtils.h>

/**
 * @brief Base class for persistable singletons using CRTP.
 *
 * Derived classes must provide:
 * - A private default constructor
 * - friend class PersistableStore<Derived>;
 * - static const char* getFilePath();
 * - String toJson() const;
 * - bool fromJson(const String& json);
 */
template <typename T>
class PersistableStore {
 protected:
  PersistableStore() = default;
  ~PersistableStore() = default;

 public:
  // Delete copy constructor and assignment
  PersistableStore(const PersistableStore&) = delete;
  PersistableStore& operator=(const PersistableStore&) = delete;

  static T& getInstance() {
    static T instance;
    return instance;
  }

  bool saveToFile() const {
    Storage.mkdir("/.crosspoint");
    const char* path = T::getFilePath();
    String json = static_cast<const T*>(this)->toJson();
    return Storage.writeFile(path, json);
  }

  bool loadFromFile() {
    const char* path = T::getFilePath();
    if (Storage.exists(path)) {
      String json = Storage.readFile(path);
      if (!json.isEmpty()) {
        return static_cast<T*>(this)->fromJson(json);
      }
    }
    return false;
  }

 protected:
  /**
   * Helper function for extracting an obfuscated password from a JSON document.
   * If the decoded password requires a resave (e.g. from plaintext fallback), `needsResave` is set to true.
   */
  std::string extractPassword(const JsonDocument& doc, bool& needsResave) const {
    bool ok = false;
    std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
    if (!ok || pass.empty()) {
      pass = doc["password"] | std::string("");
      if (!pass.empty()) needsResave = true;
    }
    return pass;
  }
};
