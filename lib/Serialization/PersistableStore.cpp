#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

bool PersistableStoreBase::writeJsonFile(const char* path, const String& json) {
  Storage.mkdir("/.crosspoint");
  if (!Storage.writeFile(path, json)) {
    LOG_ERR("PERSIST", "Failed to write %s", path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::readJsonFile(const char* path, String& jsonOut) {
  if (!Storage.exists(path)) {
    return false;  // Expected on first boot — not an error.
  }
  jsonOut = Storage.readFile(path);
  if (jsonOut.isEmpty()) {
    LOG_ERR("PERSIST", "Failed to read %s (empty)", path);
    return false;
  }
  return true;
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool ok = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
  if (!ok) {
    // Deobfuscation failed — fall back to legacy plaintext password.
    pass = doc["password"] | std::string("");
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
