#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <algorithm>
#include "FsHelpers.h"

// Wrapper to test the comparator used in FsHelpers::sortFileList
static bool naturalLess(const std::string& a, const std::string& b) {
  std::vector<std::string> v = {b, a};
  FsHelpers::sortFileList(v);
  return v[0] == a && v[1] == b && a != b;
}

static void verifyPair(const std::string& lesser, const std::string& greater) {
  EXPECT_TRUE(naturalLess(lesser, greater)) << lesser << " < " << greater;
  EXPECT_FALSE(naturalLess(greater, lesser)) << "!(" << greater << " < " << lesser << ")";
}

static void verifyEqual(const std::string& a, const std::string& b) {
  EXPECT_FALSE(naturalLess(a, b)) << "!(" << a << " < " << b << ")";
  EXPECT_FALSE(naturalLess(b, a)) << "!(" << b << " < " << a << ")";
}

TEST(NaturalSortTest, BasicAlphabetical) {
  verifyPair("apple", "banana");
  verifyPair("a", "b");
  verifyEqual("same", "same");
}

TEST(NaturalSortTest, CaseInsensitive) {
  verifyEqual("abc", "ABC");
  verifyPair("abc", "BCD");
  verifyEqual("File", "file");
}

TEST(NaturalSortTest, NumericComparison) {
  verifyPair("file1", "file2");
  verifyPair("file2", "file10");
  verifyPair("file9", "file10");
  verifyPair("file10", "file20");
  verifyPair("file99", "file100");
}

TEST(NaturalSortTest, LeadingZeros) {
  verifyEqual("file01", "file1");
  verifyEqual("file001", "file01");
  verifyPair("file01", "file2");
  verifyPair("file09", "file10");
}

TEST(NaturalSortTest, AllZeros) {
  verifyEqual("f0", "f00");
  verifyEqual("f0", "f000");
  verifyPair("f0", "f1");
  verifyPair("f00", "f1");
}

TEST(NaturalSortTest, MultipleNumericSegments) {
  verifyPair("v1.2.3", "v1.2.10");
  verifyPair("v1.9", "v1.10");
  verifyPair("v2.0", "v10.0");
}

TEST(NaturalSortTest, PrefixOrdering) {
  verifyPair("file", "file1");
  verifyPair("file", "filea");
  verifyPair("a", "ab");
  verifyPair("", "a");
  verifyEqual("", "");
}

TEST(NaturalSortTest, MixedContent) {
  verifyPair("a1b", "a2b");
  verifyPair("a1b", "a1c");
  verifyPair("a01c", "a1d");
  verifyPair("1a", "2a");
}

TEST(NaturalSortTest, DigitsVsLettersAtSamePosition) {
  verifyPair("1x", "ax");
  verifyPair("9z", "az");
}

TEST(NaturalSortTest, ChapterStyleFilenames) {
  std::vector<std::string> input = {"Chapter 10", "Chapter 1", "Chapter 20", "Chapter 2", "Chapter 3"};
  FsHelpers::sortFileList(input);
  std::vector<std::string> expected = {"Chapter 1", "Chapter 2", "Chapter 3", "Chapter 10", "Chapter 20"};
  EXPECT_EQ(input, expected);
}

TEST(NaturalSortTest, TypicalFilenames) {
  std::vector<std::string> input = {"img100.jpg", "img2.jpg", "img1.jpg", "img10.jpg", "img20.jpg"};
  FsHelpers::sortFileList(input);
  std::vector<std::string> expected = {"img1.jpg", "img2.jpg", "img10.jpg", "img20.jpg", "img100.jpg"};
  EXPECT_EQ(input, expected);
}

TEST(NaturalSortTest, Irreflexivity) {
  EXPECT_FALSE(naturalLess("abc", "abc"));
  EXPECT_FALSE(naturalLess("file10", "file10"));
  EXPECT_FALSE(naturalLess("", ""));
}

TEST(NaturalSortTest, Transitivity) {
  bool a_lt_b = naturalLess("a1", "a5");
  bool b_lt_c = naturalLess("a5", "a10");
  bool a_lt_c = naturalLess("a1", "a10");
  EXPECT_TRUE(a_lt_b && b_lt_c && a_lt_c);
}
