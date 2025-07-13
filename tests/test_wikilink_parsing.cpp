// tests/test_wikilink_parsing.cpp
#include <gtest/gtest.h>
#include "../src1/parser/Parser.h"
#include "../src1/elements/WikiLink.h"

namespace parser {
    // Fixture dla testów WikiLink
    class WikiLinkParsingTest : public ::testing::Test {
    protected:
        parser::Parser wiki_parser{""};  // Initialize with empty input
    };

    TEST_F(WikiLinkParsingTest, ParseSimpleWikiLink) {
        // Input: [[dom]]
        std::string input = "[[dom]]";

        // Call subparser for wikilink
        auto link_obj = wiki_parser.parse_wikilink(input);

        // Check if the object is not null
        ASSERT_NE(link_obj, nullptr);

        // Check target and display (no display, use target)
        EXPECT_EQ(link_obj->get_target(), "dom");
        EXPECT_FALSE(link_obj->get_display().has_value());
    }

    TEST_F(WikiLinkParsingTest, ParseWikiLinkWithDisplay) {
        // Input: [[dom|domy]]
        std::string input = "[[dom|domy]]";

        // Call the subparser for wikilinks
        auto link_obj = wiki_parser.parse_wikilink(input);

        // Check if the object is not null
        ASSERT_NE(link_obj, nullptr);

        // Check the target and display
        EXPECT_EQ(link_obj->get_target(), "dom");
        ASSERT_TRUE(link_obj->get_display().has_value());
        EXPECT_EQ(link_obj->get_display().value(), "domy");
    }

    TEST_F(WikiLinkParsingTest, ParseWikiLinkWithSuffix) {
        // nNput: [[dom]]ostwa (suffix "ostwa" - letters ASCII, isalpha)
        std::string input = "[[dom]]ostwa";

        // Call the sub-parser for wikilinks (assuming that the parser supports suffix)
        auto link_obj = wiki_parser.parse_wikilink(input);

        // Check if the object is not null
        ASSERT_NE(link_obj, nullptr);

        // Sprawdź cel i wyświetlacz (wyświetlacz = cel + sufiks)
        EXPECT_EQ(link_obj->get_target(), "dom");
        ASSERT_TRUE(link_obj->get_display().has_value());
        EXPECT_EQ(link_obj->get_display().value(), "domostwa");
    }

    TEST_F(WikiLinkParsingTest, ParseWikiLinkWithDisplayAndSuffix) {
        // Input: [[dom|domo]]stwa (suffix "stwa" - letters ASCII, isalpha)
        std::string input = "[[dom|domo]]stwa";

        // Call the sub-parser for wikilinks (assuming that the parser supports suffixes)
        auto link_obj = wiki_parser.parse_wikilink(input);

        // Check if the object is not null
        ASSERT_NE(link_obj, nullptr);

        // Check the target and display (display = display + suffix)
        EXPECT_EQ(link_obj->get_target(), "dom");
        ASSERT_TRUE(link_obj->get_display().has_value());
        EXPECT_EQ(link_obj->get_display().value(), "domostwa");
    }
}