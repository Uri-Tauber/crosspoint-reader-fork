#include <gtest/gtest.h>

#include <string>

#include "ContentOpfParser.h"

namespace {

void parse(ContentOpfParser& parser, const std::string& xml) {
  ASSERT_TRUE(parser.setup());
  EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
}

}  // namespace

TEST(ContentOpfParserMetadata, LibraryFieldsAndCalibreSeries) {
  const std::string xml = R"(<package xmlns:dc="urn:dc" xmlns:opf="urn:opf"><metadata>
    <dc:date opf:event="modification">2025-01-01</dc:date>
    <dc:date>1990</dc:date><dc:date opf:event="publication">1991-02-03</dc:date>
    <dc:publisher>  A &amp; B  </dc:publisher>
    <dc:language>en</dc:language><dc:language>fr</dc:language>
    <dc:subject>Fantasy</dc:subject><dc:subject>Adventure</dc:subject>
    <meta name="calibre:series_index" content="2.5"/>
    <meta name="calibre:series" content="Earthsea"/>
  </metadata></package>)";
  LibraryMetadata metadata;
  ContentOpfParser parser("", "", xml.size(), nullptr, false, &metadata);
  parse(parser, xml);
  EXPECT_STREQ(metadata.values[LibraryMetadata::Date], "1991-02-03");
  EXPECT_STREQ(metadata.values[LibraryMetadata::Publisher], "A & B");
  EXPECT_STREQ(metadata.values[LibraryMetadata::Language], "en");
  EXPECT_STREQ(metadata.values[LibraryMetadata::Subject], "Fantasy");
  EXPECT_STREQ(metadata.values[LibraryMetadata::Series], "Earthsea");
  EXPECT_STREQ(metadata.seriesIndex, "2.5");
  EXPECT_TRUE(metadata.calibreIndex);
}

TEST(ContentOpfParserMetadata, SeriesRefinementsCanPrecedeCollectionAndOverrideCalibre) {
  const std::string xml = R"(<package><metadata>
    <meta property="collection-type" refines="#set">set</meta>
    <meta property="belongs-to-collection" id="set">Box set</meta>
    <meta property="group-position" refines="#series">2.10.1</meta>
    <meta property="collection-type" refines="#series">series</meta>
    <meta property="belongs-to-collection" id="series">The Series</meta>
    <meta name="calibre:series" content="Fallback"/>
    <meta name="calibre:series_index" content="99"/>
  </metadata><manifest/></package>)";
  LibraryMetadata metadata;
  ContentOpfParser parser("", "", xml.size(), nullptr, true, &metadata);
  ASSERT_TRUE(parser.setup());
  EXPECT_LT(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  EXPECT_STREQ(metadata.values[LibraryMetadata::Series], "The Series");
  EXPECT_STREQ(metadata.seriesIndex, "2.10.1");
  EXPECT_FALSE(metadata.calibreIndex);
}

TEST(ContentOpfParserMetadata, BoundedLibraryTextStaysUtf8AndResetClearsPreviousBook) {
  LibraryMetadata metadata;
  const std::string xml =
      "<package><metadata><publisher>" + std::string(126, 'a') + "é</publisher></metadata></package>";
  ContentOpfParser parser("", "", xml.size(), nullptr, false, &metadata);
  parse(parser, xml);
  EXPECT_EQ(std::string(metadata.values[LibraryMetadata::Publisher]), std::string(126, 'a'));
  metadata.reset();
  EXPECT_STREQ(metadata.values[LibraryMetadata::Publisher], "");
  EXPECT_STREQ(metadata.seriesIndex, "");
}

TEST(ContentOpfParserMetadata, EntityCallbackDoesNotSplitOneAuthor) {
  const std::string xml =
      R"(<package xmlns:dc="urn:dc"><metadata><dc:creator>&#201;mile Zola</dc:creator></metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.author, "Émile Zola");
}

TEST(ContentOpfParserMetadata, SeparatesCreatorElementsAndCollapsesXmlWhitespace) {
  const std::string xml = R"(<package xmlns:dc="urn:dc"><metadata>
    <dc:title>  The
   Left Hand   of Darkness  </dc:title>
    <dc:creator> Ursula   K. Le Guin </dc:creator>
    <dc:creator>
Octavia E. Butler
</dc:creator>
  </metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.title, "The Left Hand of Darkness");
  EXPECT_EQ(parser.author, "Ursula K. Le Guin, Octavia E. Butler");
}

TEST(ContentOpfParserMetadata, StopsBeforeManifestWithoutOpeningTemporaryStorage) {
  const std::string xml = R"(<package xmlns:dc="urn:dc"><metadata>
    <dc:title>A Wizard of Earthsea</dc:title>
    <dc:creator>Ursula K. Le Guin</dc:creator>
    <dc:language>en</dc:language>
  </metadata><manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest>
  </package>)";
  Storage = {};
  ContentOpfParser parser("/missing-cache", "OPS/", xml.size(), nullptr, true);

  ASSERT_TRUE(parser.setup());
  EXPECT_LT(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  EXPECT_EQ(parser.title, "A Wizard of Earthsea");
  EXPECT_EQ(parser.author, "Ursula K. Le Guin");
  EXPECT_EQ(parser.language, "en");
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(Storage.readOpens, 0);
}

TEST(ContentOpfParserMetadata, NeverEntersManifestWhenMetadataElementIsMissing) {
  const std::string xml =
      R"(<package><manifest><item id="chapter" href="chapter.xhtml"/></manifest><spine/></package>)";
  Storage = {};
  ContentOpfParser parser("/missing-cache", "OPS/", xml.size(), nullptr, true);

  ASSERT_TRUE(parser.setup());
  EXPECT_LT(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(Storage.readOpens, 0);
}
