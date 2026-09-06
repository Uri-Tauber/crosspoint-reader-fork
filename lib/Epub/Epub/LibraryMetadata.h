#pragma once

#include <cstddef>

// Optional, bounded scratch for the library walk; the reader does not allocate it.
struct LibraryMetadata {
  static constexpr size_t TEXT_BYTES = 128;
  enum Field { Date, Publisher, Language, Series, Subject, FIELD_COUNT };
  char values[FIELD_COUNT][TEXT_BYTES]{};
  char seriesIndex[32]{};
  bool calibreIndex = false;

  void reset();
  void start(const char* name, const char** attributes);
  void text(const char* data, int length);
  void end(const char* name);

 private:
  // Bound unusual packages to eight collections, including forward refinements.
  struct Collection {
    char id[64];
    char name[TEXT_BYTES];
    char position[32];
    bool series;
  } collections[8]{};
  char pending[TEXT_BYTES]{};
  char calibreSeries[TEXT_BYTES]{};
  char calibrePosition[32]{};
  int target = -1;
  int collection = -1;
  unsigned depth = 0;
  bool spacePending = false;
  bool publicationDate = false;
  bool explicitPublicationDate = false;
  int collectionFor(const char* id);
};
