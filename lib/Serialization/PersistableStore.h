#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <string>

/**
 * @brief Non-template core of PersistableStore.
 *
 * The Storage/logging/JSON helpers live here (compiled once in
 * PersistableStore.cpp) instead of in the class template, so they are not
 * duplicated in flash for every store instantiation.
 */
class PersistableStoreBase {
 protected:
  PersistableStoreBase() = default;
  ~PersistableStoreBase() = default;

  // Writes json to path (ensures /.crosspoint exists). Logs on failure.
  static bool writeJsonFile(const char* path, const String& json);

  // Reads path into jsonOut. Returns false silently when the file does not
  // exist (expected on first boot) and with LOG_ERR when the read is empty.
  static bool readJsonFile(const char* path, String& jsonOut);

  /**
   * Helper function for extracting an obfuscated password from a JSON value.
   * Accepts JsonVariantConst so callers can pass either a whole JsonDocument
   * or a JsonObject element (e.g. inside an array iteration).
   * If the decoded password requires a resave (e.g. from plaintext fallback), `needsResave` is set to true.
   */
  static std::string extractPassword(JsonVariantConst doc, bool& needsResave);
};

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
class PersistableStore : public PersistableStoreBase {
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

  bool saveToFile() const { return writeJsonFile(T::getFilePath(), static_cast<const T*>(this)->toJson()); }

  bool loadFromFile() {
    String json;
    if (!readJsonFile(T::getFilePath(), json)) {
      return false;
    }
    return static_cast<T*>(this)->fromJson(json);
  }
};
