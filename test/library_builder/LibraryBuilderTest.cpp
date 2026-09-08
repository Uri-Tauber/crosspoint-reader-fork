#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "Epub.h"
#include "LibraryBuilder.h"
#include "LibraryIndexFile.h"
#include "LibrarySorter.h"

using namespace library;
namespace {
constexpr char INDEX[] = "/.crosspoint/library.idx";

struct ExpectedBook {
  std::string path;
  std::string title;
  std::string author;
  std::string series;
};

std::string numbered(const char* prefix, unsigned value) {
  char text[32];
  snprintf(text, sizeof(text), "%s%04u", prefix, value);
  return text;
}

std::string pathAt(LibraryIndexFile& index, SortOrder order, uint16_t row) {
  const uint16_t ordinal = index.ordinalForRow(order, row);
  if (ordinal == 0xffff) return {};
  ClixRecord record{};
  if (!index.readRecord(ordinal, record)) return {};
  std::string path;
  return index.readPath(record, path) ? path : std::string();
}

class LibraryBuilderTest : public ::testing::Test {
 protected:
  BuildStats stats;
  void SetUp() override {
    fake::reset();
    bookMetadata.clear();
    fake::add("/a.epub");
    fake::add("/b.epub");
  }
  void initial(uint16_t mask = DEFAULT_SORTS) { ASSERT_TRUE(buildLibraryIndex("/", stats, true, mask)); }
};
}  // namespace
TEST_F(LibraryBuilderTest, UnchangedDoesNotParseSortOrReplace) {
  initial();
  const auto bytes = fake::files[INDEX]->bytes;
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 0u);
  EXPECT_EQ(stats.parsed, 0);
  EXPECT_EQ(stats.metadataReused, 2);
  EXPECT_EQ(stats.sortPasses, 0);
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, bytes);
}
TEST_F(LibraryBuilderTest, OnlyChangedTimestampOrSizeParses) {
  initial();
  fake::parses = 0;
  fake::files["/a.epub"]->time++;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.metadataReused, 1);
  fake::files["/b.epub"]->bytes.push_back('x');
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
}
TEST_F(LibraryBuilderTest, MissingTimestampFailedExtractionAndForceRefreshRetry) {
  fake::files["/a.epub"]->time = 0;
  bookMetadata["/b.epub"].success = false;
  initial();
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 2u);
  fake::files["/a.epub"]->time = 1;
  bookMetadata["/b.epub"].success = true;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, DEFAULT_SORTS, true));
  EXPECT_EQ(fake::parses, 2u);
}
TEST_F(LibraryBuilderTest, DuplicateBasenamesUseFullPathAndRenamesOnlyKeepArrival) {
  fake::add("/other/a.epub");
  bookMetadata["/other/a.epub"].title = "Other";
  initial();
  fake::parses = 0;
  fake::files["/other/a.epub"]->time++;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
  EXPECT_TRUE(Storage.rename("/a.epub", "/renamed.epub"));
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.renamed, 1);
}
TEST_F(LibraryBuilderTest, DisabledModeNeverParsesAndCannotBlessChangedMetadata) {
  initial();
  fake::parses = 0;
  fake::files["/a.epub"]->time++;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false, DEFAULT_SORTS, true));
  EXPECT_EQ(fake::parses, 0u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  auto metadata = std::make_unique<CachedMetadata>();
  ASSERT_TRUE(index.readMetadata(0, *metadata));
  EXPECT_FALSE(metadata->extracted);
  index.close();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 2u);
}
TEST_F(LibraryBuilderTest, PreferencePreparationHasNoWalkOrParsesAndRetainsOrders) {
  initial();
  fake::walks = fake::parses = 0;
  constexpr uint16_t series = 1u << static_cast<unsigned>(SortKind::Series);
  constexpr uint16_t language = 1u << static_cast<unsigned>(SortKind::Language);
  ASSERT_TRUE(prepareLibraryOrders(series, stats));
  EXPECT_EQ(fake::walks, 0u);
  EXPECT_EQ(fake::parses, 0u);
  EXPECT_GT(stats.sortPasses, 0);
  ASSERT_TRUE(prepareLibraryOrders(language, stats));
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_TRUE(index.hasOrders(series | language));
  index.close();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, series));
  EXPECT_FALSE(stats.indexReplaced);
  fake::files["/a.epub"]->time++;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, series));
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_TRUE(index.hasOrders(series));
  EXPECT_FALSE(index.hasOrders(language));
}
TEST_F(LibraryBuilderTest, CanonicalAuthorVotesUseOriginalSpellings) {
  fake::add("/c.epub");
  bookMetadata["/a.epub"].author = "Lu Xun";
  bookMetadata["/b.epub"].author = "Xun, Lu";
  bookMetadata["/c.epub"].author = "Xun, Lu";
  initial();
  Storage.remove("/c.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ClixRecord record{};
  std::string author;
  ASSERT_TRUE(index.readRecord(0, record));
  ASSERT_TRUE(index.readAuthor(record, author));
  EXPECT_EQ(author, "Lu Xun");
}
TEST_F(LibraryBuilderTest, PreviousFormatIsRejectedAndRebuilt) {
  initial();
  fake::files[INDEX]->bytes[4] = CLIX_FORMAT_VERSION - 1;
  LibraryIndexFile index;
  EXPECT_FALSE(index.open(INDEX));
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 2u);
}
TEST_F(LibraryBuilderTest, FailedInstallationAndInterruptedBackupRetainIndex) {
  initial();
  auto old = fake::files[INDEX]->bytes;
  fake::files["/a.epub"]->time++;
  fake::failRename = 1;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  ASSERT_TRUE(Storage.rename(INDEX, "/.crosspoint/library.bak"));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_TRUE(Storage.exists(INDEX));
}
TEST_F(LibraryBuilderTest, AllocationFailuresRetainUsableIndex) {
  initial();
  const auto files = fake::files;
  const auto old = fake::files[INDEX]->bytes;
  for (int failure = 0; failure < 18; ++failure) {
    fake::files = files;
    fake::files["/a.epub"]->time = 2;
    fake::failAlloc = failure;
    fake::failureTriggered = false;
    const bool ok = buildLibraryIndex("/", stats, true, 1u << 6);
    fake::failAlloc = -1;
    if (!ok)
      ASSERT_EQ(fake::files[INDEX]->bytes, old) << failure;
    else
      EXPECT_FALSE(fake::failureTriggered) << "unexpected success after allocation failure " << failure;
  }
}
TEST_F(LibraryBuilderTest, PreparationReadAndWriteFailuresRetainIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;
  for (int failure : {0, 1, 3, 8, 20}) {
    fake::failRead = failure;
    fake::files[INDEX]->bytes = old;
    if (!prepareLibraryOrders(1u << 6, stats)) {
      EXPECT_EQ(fake::files[INDEX]->bytes, old);
    }
    fake::failRead = -1;
  }
  for (int failure : {0, 1, 3, 8}) {
    fake::files[INDEX]->bytes = old;
    fake::failWrite = failure;
    fake::failureTriggered = false;
    const bool ok = prepareLibraryOrders(1u << 6, stats);
    if (fake::failureTriggered) {
      EXPECT_FALSE(ok);
      EXPECT_EQ(fake::files[INDEX]->bytes, old);
    }
    fake::failWrite = -1;
  }
}
TEST(LibraryBufferedSorter, MatchesReferenceAcrossBatchAndMergeBoundaries) {
  fake::reset();
  std::mt19937 random(77);
  for (unsigned count : {0, 1, 7, 8, 9, 17, 127, 512, 4096}) {
    std::vector<BufferedSortKey> keys(count);
    for (unsigned i = 0; i < count; ++i) {
      auto& key = keys[i];
      snprintf(key.text, sizeof(key.text), "%s", i % 9 ? (i % 3 ? "alpha" : "beta") : "");
      key.date = i % 7 ? random() % 200 : 0;
      key.series = seriesPositionKey(std::to_string(random() % 17).c_str(), true);
    }
    auto workspace = std::make_unique<SortWorkspace>();
    for (uint8_t field = 0; field < METADATA_SORT_COUNT; ++field) {
      std::vector<uint16_t> actual(count), expected(count);
      std::iota(expected.begin(), expected.end(), 0);
      std::stable_sort(expected.begin(), expected.end(), [&](auto a, auto b) {
        const int compared = compareMetadataSortKeys(&keys[a], &keys[b], field);
        return compared < 0 || (compared == 0 && a < b);
      });
      uint16_t passes = 0;
      ASSERT_TRUE(bufferedSort(
          count, field, sizeof(BufferedSortKey),
          [](void* ctx, uint16_t ordinal, uint8_t, void* output) {
            *static_cast<BufferedSortKey*>(output) = (*static_cast<std::vector<BufferedSortKey>*>(ctx))[ordinal];
            return true;
          },
          compareMetadataSortKeys, &keys, *workspace, actual.data(), passes));
      EXPECT_EQ(actual, expected) << count << ":" << unsigned(field);
    }
  }
}

TEST_F(LibraryBuilderTest, RebuildReadAndWriteFailuresRetainIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;
  fake::files["/a.epub"]->time++;
  for (bool writing : {false, true}) {
    for (int failure = 0; failure < 100; ++failure) {
      fake::files[INDEX]->bytes = old;
      fake::failureTriggered = false;
      (writing ? fake::failWrite : fake::failRead) = failure;
      const bool ok = buildLibraryIndex("/", stats, true, 1u << 6);
      fake::failRead = fake::failWrite = -1;
      if (fake::failureTriggered) {
        EXPECT_FALSE(ok) << writing << ":" << failure;
        EXPECT_EQ(fake::files[INDEX]->bytes, old) << writing << ":" << failure;
      }
    }
  }
}
TEST_F(LibraryBuilderTest, CachedDisplayAndOpeningReadOnlyRequestedData) {
  bookMetadata["/a.epub"].title = "Book title";
  bookMetadata["/a.epub"].author = "Writer";
  initial();
  LibraryIndexFile index;
  const auto before = fake::reads;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(fake::reads - before, sizeof(ClixHeader));
  ClixRecord record{};
  ASSERT_TRUE(index.readRecord(0, record));
  std::string title, value;
  ASSERT_TRUE(index.readDisplay(record, SortKind::Author, title, value));
  EXPECT_EQ(title, "Book title");
  EXPECT_EQ(value, "Writer");
  ASSERT_TRUE(index.readDisplay(record, SortKind::Series, title, value));
  EXPECT_EQ(title, "Book title");
  EXPECT_EQ(value, "Series");
  EXPECT_FALSE(index.hasOrders(1u << 6));
  EXPECT_EQ(index.ordinalForRow(SortOrder::SeriesAsc, 0), 0xffff);
}
TEST_F(LibraryBuilderTest, EmptyLibraryRemainsUsable) {
  fake::files.clear();
  fake::add("/", "");
  fake::files["/"]->directory = true;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(stats.books, 0);
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_FALSE(stats.indexReplaced);
}

TEST_F(LibraryBuilderTest, OldSortBoundaryKeepsCoreAndExtendedOrdersCorrect) {
  fake::files.clear();
  std::vector<ExpectedBook> books;
  books.reserve(513);
  for (unsigned i = 0; i < 512; ++i) {
    ExpectedBook book{"/book" + numbered("", i) + ".epub", numbered("Title ", 511 - i),
                      numbered("Given S", (i * 257) % 512), numbered("Series ", (i * 37) % 512)};
    fake::add(book.path);
    auto& metadata = bookMetadata[book.path];
    metadata.title = book.title;
    metadata.author = book.author;
    metadata.series = book.series;
    books.push_back(std::move(book));
  }

  constexpr uint16_t sorts = DEFAULT_SORTS | (1u << static_cast<unsigned>(SortKind::Series));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, sorts));

  ExpectedBook added{"/000-new.epub", "Title 0256a", "Given S0256a", "Series 0256a"};
  fake::add(added.path);
  auto& metadata = bookMetadata[added.path];
  metadata.title = added.title;
  metadata.author = added.author;
  metadata.series = added.series;
  books.push_back(added);

  ASSERT_TRUE(buildLibraryIndex("/", stats, true, sorts));
  ASSERT_EQ(stats.books, 513);
  EXPECT_GT(stats.sortPasses, 0);

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_TRUE(index.hasOrders(sorts));

  std::vector<std::string> expectedAdded;
  expectedAdded.reserve(books.size());
  for (const auto& book : books) expectedAdded.push_back(book.path);

  const auto expectSorted = [&](SortOrder order, auto member) {
    auto expected = books;
    std::stable_sort(expected.begin(), expected.end(),
                     [member](const auto& a, const auto& b) { return a.*member < b.*member; });
    for (uint16_t row = 0; row < expected.size(); ++row) {
      EXPECT_EQ(pathAt(index, order, row), expected[row].path) << row;
    }
  };

  for (uint16_t row = 0; row < expectedAdded.size(); ++row) {
    EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, row), expectedAdded[row]) << row;
  }
  expectSorted(SortOrder::TitleAsc, &ExpectedBook::title);
  expectSorted(SortOrder::AuthorAsc, &ExpectedBook::author);
  expectSorted(SortOrder::SeriesAsc, &ExpectedBook::series);
}

TEST_F(LibraryBuilderTest, MaximumLibraryKeepsCoreOrdersCorrect) {
  fake::files.clear();
  std::vector<ExpectedBook> books;
  books.reserve(CLIX_MAX_RECORDS);
  for (unsigned i = 0; i < CLIX_MAX_RECORDS; ++i) {
    ExpectedBook book{"/scan" + numbered("", i) + ".epub",
                      numbered("Title ", CLIX_MAX_RECORDS - 1 - i),
                      numbered("Given S", (i * 2053) % CLIX_MAX_RECORDS),
                      {}};
    fake::add(book.path);
    auto& metadata = bookMetadata[book.path];
    metadata.title = book.title;
    metadata.author = book.author;
    books.push_back(std::move(book));
  }

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  ASSERT_EQ(stats.books, CLIX_MAX_RECORDS);
  EXPECT_GT(stats.sortPasses, 0);

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_TRUE(index.hasOrders(DEFAULT_SORTS));

  for (uint16_t row = 0; row < books.size(); ++row) {
    EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, row), books[row].path) << row;
    EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, row), books[books.size() - 1 - row].path) << row;
  }

  std::stable_sort(books.begin(), books.end(), [](const auto& a, const auto& b) { return a.author < b.author; });
  for (uint16_t row = 0; row < books.size(); ++row) {
    EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, row), books[row].path) << row;
  }
}

TEST_F(LibraryBuilderTest, WriteCloseFailureRetainsIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;
  for (int failure : {0, 1}) {
    fake::files[INDEX]->bytes = old;
    fake::failClose = failure;
    const bool ok = prepareLibraryOrders(1u << 6, stats);
    fake::failClose = -1;
    EXPECT_FALSE(ok);
    EXPECT_EQ(fake::files[INDEX]->bytes, old);
  }
}

TEST_F(LibraryBuilderTest, UnchangedBeyondDedupLimitRetainsIndex) {
  for (unsigned i = 0; i < 1025; ++i) fake::add("/" + std::to_string(i) + ".txt");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_TRUE(stats.dedupDegraded);
  const auto old = fake::files[INDEX]->bytes;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(stats.sortPasses, 0);
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}
