#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "TxtPageIndex.h"

namespace {

constexpr const char* CACHE_DIR = "/cache";
constexpr const char* FINAL_PATH = "/cache/index.bin";
constexpr const char* PARTIAL_PATH = "/cache/index.bin.part";

TxtIndexSpec spec() { return TxtIndexSpec{1000, 720, 24, 7, 12, 3}; }

std::vector<uint8_t> completeIndexBytes() {
  TxtPageIndex index;
  EXPECT_TRUE(index.openOrStart(CACHE_DIR, spec()));
  EXPECT_TRUE(index.appendPageOffset(250));
  EXPECT_TRUE(index.finish());
  return Storage.file(FINAL_PATH);
}

class TxtPageIndexTest : public ::testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
};

TEST_F(TxtPageIndexTest, AppendsLooksUpAndReopensCompletedIndex) {
  TxtPageIndex index;
  ASSERT_TRUE(index.openOrStart(CACHE_DIR, spec()));
  EXPECT_FALSE(index.isComplete());
  EXPECT_EQ(index.knownPageCount(), 1u);

  ASSERT_TRUE(index.appendPageOffset(250));
  ASSERT_TRUE(index.appendPageOffset(700));
  uint32_t offset = 0;
  ASSERT_TRUE(index.pageOffset(1, offset));
  EXPECT_EQ(offset, 250u);
  EXPECT_FALSE(index.pageOffset(3, offset));

  ASSERT_TRUE(index.finish());
  EXPECT_TRUE(index.isComplete());
  EXPECT_TRUE(Storage.hasFile(FINAL_PATH));
  EXPECT_FALSE(Storage.hasFile(PARTIAL_PATH));
  EXPECT_EQ(Storage.file(FINAL_PATH).size(), 36u + 3u * sizeof(uint32_t));

  TxtPageIndex reopened;
  ASSERT_TRUE(reopened.openOrStart(CACHE_DIR, spec()));
  EXPECT_TRUE(reopened.isComplete());
  EXPECT_EQ(reopened.knownPageCount(), 3u);
  ASSERT_TRUE(reopened.pageOffset(2, offset));
  EXPECT_EQ(offset, 700u);
}

TEST_F(TxtPageIndexTest, RejectsIncompleteCanonicalFile) {
  auto bytes = completeIndexBytes();
  bytes[5] = 0;  // Header state byte.
  Storage.setFile(FINAL_PATH, std::move(bytes));

  TxtPageIndex index;
  ASSERT_TRUE(index.openOrStart(CACHE_DIR, spec()));
  EXPECT_FALSE(index.isComplete());
  EXPECT_EQ(index.knownPageCount(), 1u);
  EXPECT_TRUE(Storage.hasFile(PARTIAL_PATH));
}

TEST_F(TxtPageIndexTest, InterruptedReplacementPreservesPreviousCompleteCache) {
  const auto original = completeIndexBytes();
  auto changed = spec();
  changed.viewportWidth++;

  {
    TxtPageIndex replacement;
    ASSERT_TRUE(replacement.openOrStart(CACHE_DIR, changed));
    EXPECT_FALSE(replacement.isComplete());
    ASSERT_TRUE(replacement.appendPageOffset(300));
    EXPECT_EQ(Storage.file(FINAL_PATH), original);
  }

  EXPECT_EQ(Storage.file(FINAL_PATH), original);
  EXPECT_FALSE(Storage.hasFile(PARTIAL_PATH));
}

TEST_F(TxtPageIndexTest, RejectsTruncatedCompletedCache) {
  auto bytes = completeIndexBytes();
  bytes.pop_back();
  Storage.setFile(FINAL_PATH, std::move(bytes));

  TxtPageIndex index;
  ASSERT_TRUE(index.openOrStart(CACHE_DIR, spec()));
  EXPECT_FALSE(index.isComplete());
  EXPECT_EQ(index.knownPageCount(), 1u);
}

TEST_F(TxtPageIndexTest, EveryPaginationInputInvalidatesTheCache) {
  const auto original = completeIndexBytes();
  std::vector<TxtIndexSpec> changedSpecs(6, spec());
  changedSpecs[0].sourceFileSize++;
  changedSpecs[1].viewportWidth++;
  changedSpecs[2].linesPerPage++;
  changedSpecs[3].fontId++;
  changedSpecs[4].screenMargin++;
  changedSpecs[5].paragraphAlignment++;

  for (const auto& changed : changedSpecs) {
    Storage.setFile(FINAL_PATH, original);
    TxtPageIndex index;
    ASSERT_TRUE(index.openOrStart(CACHE_DIR, changed));
    EXPECT_FALSE(index.isComplete());
  }
}

}  // namespace
