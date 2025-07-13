// tests/test_full_parsing.cpp
#include <gtest/gtest.h>  // Google Test
#include "../src1/parser/Parser.h"  // Main parser or subparser

namespace parser {
    // Fixture for tag tests
    class FullParsingTest : public ::testing::Test {
    protected:
        parser::Parser wiki_parser{""};  // Initialize with empty input
    };

    TEST_F(FullParsingTest, ParseInvalidNonTagInText) {
        // Input: m<n && m>a - invalid, no proper tag (name "n" not in factory)
        std::string input = "m<n && m>a </n>";

        // Use full parse
        wiki_parser = parser::Parser(input);
        auto elements = wiki_parser.parse();

        // Expect TextElement or invalid Tag (adjust based on lexer behavior)
        ASSERT_EQ(3, elements.size());  // Whole as text or invalid tag
        auto text_0 = dynamic_cast<elements::TextElement*>(elements[0].get());
        auto text_1 = dynamic_cast<elements::TextElement*>(elements[1].get());
        auto text_2 = dynamic_cast<elements::TextElement*>(elements[2].get());
        EXPECT_EQ(input, text_0->get_text() + text_1->get_text() + text_2->get_text());
    }

    TEST_F(FullParsingTest, ParseValidTagInText) {
        // Input: m<span && m>a - valid tag <span> (name "span" in factory)
        std::string input = "m<span && m>a </span>";

        // Use full parse
        wiki_parser = parser::Parser(input);
        auto elements = wiki_parser.parse();

        // Expect 3 elements: text "m", Tag "span", text " && m>a"
        ASSERT_EQ(2, elements.size());

        // First: text "m"
        auto text1 = dynamic_cast<elements::TextElement*>(elements[0].get());
        ASSERT_NE(nullptr, text1);
        EXPECT_EQ("m", text1->get_text());

        // Second: valid opening Tag "span"
        auto tag_content = dynamic_cast<elements::TaggedContent*>(elements[1].get());
        ASSERT_NE(nullptr ,tag_content);
        auto tag = tag_content->get_opening_tag();
        ASSERT_NE(nullptr ,tag);
        EXPECT_EQ("span", tag->get_name());
        EXPECT_TRUE(tag->is_opening());
        EXPECT_TRUE(tag->is_valid());
        EXPECT_TRUE(tag->get_attributes().empty());
    }
}