#include <gtest/gtest.h>

#include <cstring>
#include <utility>
#include <vector>

#include "LibraryIndexFile.h"

namespace library {

std::string joinLibraryPath(const std::string_view folder, const std::string_view name) {
  return std::string(folder) + "/" + std::string(name);
}

}  // namespace library

TEST(LibraryIndexFile, MissingIndexDoesNotCloseAnUninitializedHandle) {
  Storage.clearFile();
  HalFile::resetInvalidCloseCount();

  {
    library::LibraryIndexFile index;
    EXPECT_FALSE(index.open("/missing.clx"));
  }

  EXPECT_EQ(HalFile::invalidCloseCount(), 0);
}

TEST(LibraryIndexFile, ReadsEveryStoredOrderInBothDirections) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 3;
  header.preparedSorts = 0xff;
  library::layoutSections(header, 0, 0);

  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t authorOrder[] = {2, 0, 1};
  const uint16_t arrivalOrder[] = {1, 2, 0};
  std::memcpy(bytes.data() + library::authorOrderOffset(header, 0), authorOrder, sizeof(authorOrder));
  std::memcpy(bytes.data() + library::arrivalOrderOffset(header, 0), arrivalOrder, sizeof(arrivalOrder));
  for (uint8_t i = 3; i < library::SORT_COUNT; ++i) {
    std::memcpy(bytes.data() + library::metadataOrderOffset(header, static_cast<library::SortKind>(i), 0), authorOrder,
                sizeof(authorOrder));
  }
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));

  const auto expectOrder = [&](const library::SortOrder order, const uint16_t a, const uint16_t b, const uint16_t c) {
    EXPECT_EQ(index.ordinalForRow(order, 0), a);
    EXPECT_EQ(index.ordinalForRow(order, 1), b);
    EXPECT_EQ(index.ordinalForRow(order, 2), c);
    EXPECT_EQ(index.ordinalForRow(order, 3), 0xFFFF);
  };
  expectOrder(library::SortOrder::AddedAsc, 1, 2, 0);
  expectOrder(library::SortOrder::AddedDesc, 0, 2, 1);
  expectOrder(library::SortOrder::TitleAsc, 0, 1, 2);
  expectOrder(library::SortOrder::TitleDesc, 2, 1, 0);
  expectOrder(library::SortOrder::AuthorAsc, 2, 0, 1);
  expectOrder(library::SortOrder::AuthorDesc, 1, 0, 2);
  for (uint8_t i = 3; i < library::SORT_COUNT; ++i) {
    expectOrder(library::sortOrder(static_cast<library::SortKind>(i), false), 2, 0, 1);
    expectOrder(library::sortOrder(static_cast<library::SortKind>(i), true), 1, 0, 2);
  }
}

TEST(LibraryIndexFile, ReadsExtendedBlobAndRejectsTruncatedField) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  const uint8_t blob[] = {'x', 1, 'a', 1, 't', 4, '2', '0', '0', '1', 1, 'p', 2, 'e', 'n', 1, 's', 1, 'g'};
  library::layoutSections(header, 0, sizeof(blob));
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.nameStart, blob, sizeof(blob));
  library::ClixRecord record{};
  record.nameLen = 1;
  Storage.setFile("/library.clx", bytes);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  std::string value;
  EXPECT_TRUE(index.readSortValue(record, library::SortKind::Language, value));
  EXPECT_EQ(value, "en");
  EXPECT_TRUE(index.readSortValue(record, library::SortKind::Subject, value));
  EXPECT_EQ(value, "g");
  index.close();
  bytes[header.nameStart + header.nameLen - 2] = 255;
  Storage.setFile("/library.clx", std::move(bytes));
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_FALSE(index.readSortValue(record, library::SortKind::Subject, value));
}

TEST(LibraryIndexFile, PreviousFormatIsRejected) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = 1;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 200;
  library::layoutSections(header, 0, 0);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  Storage.setFile("/library.clx", std::move(bytes));
  library::LibraryIndexFile index;
  EXPECT_FALSE(index.open("/library.clx"));
}
