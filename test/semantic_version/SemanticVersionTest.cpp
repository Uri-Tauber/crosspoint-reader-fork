#include <gtest/gtest.h>

#include "SemanticVersion.h"

namespace {

using semantic_version::Version;

Version parsed(const char* text) {
  Version version;
  EXPECT_TRUE(semantic_version::parse(text, version)) << text;
  return version;
}

TEST(SemanticVersion, ParsesReleaseAndBuildFormats) {
  const Version stable = parsed("v1.5.0");
  EXPECT_EQ(stable.major, 1U);
  EXPECT_EQ(stable.minor, 5U);
  EXPECT_EQ(stable.patch, 0U);
  EXPECT_FALSE(stable.releaseCandidate);

  EXPECT_TRUE(parsed("1.6.0-rc+abc123").releaseCandidate);
  EXPECT_TRUE(parsed("1.6.0rc").releaseCandidate);
  EXPECT_FALSE(parsed("1.6.0-dev-fix/ota-version-deadbee").releaseCandidate);
  EXPECT_FALSE(parsed("1.6.0+build.7").releaseCandidate);
}

TEST(SemanticVersion, RejectsMalformedNumericCores) {
  Version version;
  EXPECT_FALSE(semantic_version::parse("", version));
  EXPECT_FALSE(semantic_version::parse("v1.2", version));
  EXPECT_FALSE(semantic_version::parse("1.2.3.4", version));
  EXPECT_FALSE(semantic_version::parse("1.2.3 unknown", version));
  EXPECT_FALSE(semantic_version::parse("4294967296.0.0", version));
}

TEST(SemanticVersion, ComparesNumericCore) {
  EXPECT_TRUE(semantic_version::isNewer(parsed("2.0.0"), parsed("1.99.99")));
  EXPECT_TRUE(semantic_version::isNewer(parsed("1.6.0"), parsed("1.5.99")));
  EXPECT_TRUE(semantic_version::isNewer(parsed("1.5.1"), parsed("1.5.0")));
  EXPECT_FALSE(semantic_version::isNewer(parsed("1.5.0"), parsed("1.5.0")));
  EXPECT_FALSE(semantic_version::isNewer(parsed("1.4.9"), parsed("1.5.0")));
}

TEST(SemanticVersion, StableReleaseSupersedesMatchingRc) {
  EXPECT_TRUE(semantic_version::isNewer(parsed("v1.6.0"), parsed("1.6.0-rc+abc123")));
  EXPECT_TRUE(semantic_version::isNewer(parsed("1.6.0"), parsed("1.6.0rc")));
  EXPECT_FALSE(semantic_version::isNewer(parsed("1.6.0rc"), parsed("1.6.0")));
  EXPECT_FALSE(semantic_version::isNewer(parsed("1.6.0rc"), parsed("1.6.0-rc+abc123")));
}

}  // namespace
