#include "LibraryMetadata.h"

#include <Logging.h>
#include <XmlParserUtils.h>

#include <cstdio>
#include <cstring>

namespace {
const char* attribute(const char** attributes, const char* name) {
  for (int i = 0; attributes[i]; i += 2) {
    if (xmlLocalNameEquals(attributes[i], name)) return attributes[i + 1];
  }
  return "";
}
void copyText(char* destination, size_t size, const char* source) {
  size_t n = strlen(source);
  if (n >= size) {
    n = size - 1;
    while (n && (static_cast<unsigned char>(source[n]) & 0xc0) == 0x80) --n;
  }
  if (n) {
    size_t start = n - 1;
    while (start && (static_cast<unsigned char>(source[start]) & 0xc0) == 0x80) --start;
    const auto lead = static_cast<unsigned char>(source[start]);
    const size_t expected = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
    if (n - start < expected) n = start;
  }
  memcpy(destination, source, n);
  destination[n] = 0;
}
}  // namespace

void LibraryMetadata::reset() {
  memset(this, 0, sizeof(*this));
  target = collection = -1;
}

int LibraryMetadata::collectionFor(const char* id) {
  if (strlen(id) >= sizeof(collections[0].id)) return -1;
  for (int i = 0; i < 8; ++i) {
    if (collections[i].id[0] && strcmp(collections[i].id, id) == 0) return i;
  }
  for (int i = 0; i < 8; ++i) {
    if (!collections[i].id[0] && !collections[i].name[0]) {
      copyText(collections[i].id, sizeof(collections[i].id), id);
      return i;
    }
  }
  LOG_DBG("LIBMETA", "Collection limit reached");
  return -1;
}

void LibraryMetadata::start(const char* name, const char** attributes) {
  if (depth++) return;
  target = -1;
  collection = -1;
  pending[0] = 0;
  spacePending = false;
  publicationDate = false;
  if (xmlLocalNameEquals(name, "date")) {
    const char* event = attribute(attributes, "event");
    publicationDate = strcmp(event, "publication") == 0;
    if (!*event || publicationDate) target = Date;
  } else if (xmlLocalNameEquals(name, "publisher"))
    target = Publisher;
  else if (xmlLocalNameEquals(name, "language"))
    target = Language;
  else if (xmlLocalNameEquals(name, "subject"))
    target = Subject;
  else if (xmlLocalNameEquals(name, "meta")) {
    const char* key = attribute(attributes, "name");
    const char* content = attribute(attributes, "content");
    if (strcmp(key, "calibre:series") == 0) copyText(calibreSeries, sizeof(calibreSeries), content);
    if (strcmp(key, "calibre:series_index") == 0) copyText(calibrePosition, sizeof(calibrePosition), content);
    const char* property = attribute(attributes, "property");
    const char* refines = attribute(attributes, "refines");
    if (strcmp(property, "belongs-to-collection") == 0 && !*refines) {
      collection = collectionFor(attribute(attributes, "id"));
      target = FIELD_COUNT;
    } else if (*refines == '#' &&
               (strcmp(property, "collection-type") == 0 || strcmp(property, "group-position") == 0)) {
      collection = collectionFor(refines + 1);
      target = FIELD_COUNT + (strcmp(property, "collection-type") == 0 ? 1 : 2);
    }
  }
}

void LibraryMetadata::text(const char* data, int length) {
  if (!depth || target < 0) return;
  size_t used = strlen(pending);
  for (int i = 0; i < length; ++i) {
    const char c = data[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      spacePending = used != 0;
      continue;
    }
    if (spacePending && used + 1 < sizeof(pending)) pending[used++] = ' ';
    spacePending = false;
    if (used + 1 < sizeof(pending)) pending[used++] = c;
  }
  pending[used] = 0;
}

void LibraryMetadata::end(const char* name) {
  if (!depth) {
    if (xmlLocalNameEquals(name, "metadata")) {
      copyText(values[Series], TEXT_BYTES, calibreSeries);
      copyText(seriesIndex, sizeof(seriesIndex), calibrePosition);
      calibreIndex = true;
      for (const auto& item : collections) {
        if (item.series && item.name[0]) {
          copyText(values[Series], TEXT_BYTES, item.name);
          copyText(seriesIndex, sizeof(seriesIndex), item.position);
          calibreIndex = false;
          break;
        }
      }
    }
    return;
  }
  if (--depth) return;
  if (target >= 0 && target < FIELD_COUNT && pending[0]) {
    if (!values[target][0] || (target == Date && publicationDate && !explicitPublicationDate)) {
      copyText(values[target], TEXT_BYTES, pending);
      if (target == Date && publicationDate) explicitPublicationDate = true;
    }
  } else if (collection >= 0) {
    auto& item = collections[collection];
    if (target == FIELD_COUNT) copyText(item.name, sizeof(item.name), pending);
    if (target == FIELD_COUNT + 1) item.series = strcmp(pending, "series") == 0;
    if (target == FIELD_COUNT + 2) copyText(item.position, sizeof(item.position), pending);
  }
}
