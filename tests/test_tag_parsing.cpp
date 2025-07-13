// tests/test_tag_parsing.cpp
#include <gtest/gtest.h>  // Google Test
#include "../src1/parser/Parser.h"  // Main parser or subparser
#include "../src1/elements/Tag.h"  // Tag class

namespace parser {
    // Fixture for tag tests
    class TagParsingTest : public ::testing::Test {
    protected:
        parser::Parser wiki_parser{""};  // Initialize with empty input
    };

    TEST_F(TagParsingTest, ParseOpeningTag) {
        // Input: <sub>
        std::string input = "<sub>";

        // Call tag subparser
        size_t pos = 0;
        auto tag_obj = wiki_parser.parse_tag(input, pos);

        // Check if object is not null
        ASSERT_NE(tag_obj, nullptr);

        // Check name, type, valid, no attributes
        EXPECT_EQ("sub", tag_obj->get_name());
        EXPECT_TRUE(tag_obj->is_opening());
        EXPECT_TRUE(tag_obj->is_valid());
        EXPECT_TRUE(tag_obj->get_attributes().empty());
    }

    TEST_F(TagParsingTest, ParseClosingTag) {
        // Input: </sub>
        std::string input = "</sub>";

        // Call tag subparser
        size_t pos = 0;
        auto tag_obj = wiki_parser.parse_tag(input, pos);

        // Check if object is not null
        ASSERT_NE(tag_obj, nullptr);

        // Check name, type, valid, no attributes
        EXPECT_EQ("sub", tag_obj->get_name());
        EXPECT_TRUE(tag_obj->is_closing());
        EXPECT_TRUE(tag_obj->is_valid());
        EXPECT_TRUE(tag_obj->get_attributes().empty());
    }

    TEST_F(TagParsingTest, ParseSelfClosingTagWithSpaces) {
        // Input: <br  /> (spaces between name and / allowed)
        std::string input = "<br  />";

        // Call tag subparser
        size_t pos = 0;
        auto tag_obj = wiki_parser.parse_tag(input, pos);

        // Check if object is not null
        ASSERT_NE(tag_obj, nullptr);

        // Check name, type, valid, no attributes
        EXPECT_EQ("br", tag_obj->get_name());
        EXPECT_TRUE(tag_obj->is_self_closing());
        EXPECT_TRUE(tag_obj->is_valid());
        EXPECT_TRUE(tag_obj->get_attributes().empty());
    }

    TEST_F(TagParsingTest, ParseTagWithDoubleQuotedAttributes) {
        // Input: <span style="color:red;" class="highlight" title="example">
        std::string input = "<span style=\"color:red;\" class=\"highlight\" title=\"example\">";

        // Call tag subparser
        size_t pos = 0;
        auto tag_obj = wiki_parser.parse_tag(input, pos);

        // Check if object is not null
        ASSERT_NE(tag_obj, nullptr);

        // Check name, type, valid
        EXPECT_EQ( "span", tag_obj->get_name());
        EXPECT_TRUE(tag_obj->is_opening());
        EXPECT_TRUE(tag_obj->is_valid());

        // Check attributes
        const auto& attrs = tag_obj->get_attributes();
        EXPECT_EQ(attrs.size(), 3);
        EXPECT_EQ("color:red;", attrs.at("style"));
        EXPECT_EQ("highlight",  attrs.at("class"));
        EXPECT_EQ("example",    attrs.at("title"));
    }

    TEST_F(TagParsingTest, ParseTagWithMixedQuotedAttributes) {
        // Input: <span style='color:red' class=highlight>
        std::string input = "<span style='color:red' class=highlight>";

        // Call tag subparser
        size_t pos = 0;
        auto tag_obj = wiki_parser.parse_tag(input, pos);

        // Check if object is not null
        ASSERT_NE(tag_obj, nullptr);

        // Check name, type, valid
        EXPECT_EQ("span", tag_obj->get_name());
        EXPECT_TRUE(tag_obj->is_opening());
        EXPECT_TRUE(tag_obj->is_valid());

        // Check attributes
        const auto& attrs = tag_obj->get_attributes();
        EXPECT_EQ(2, attrs.size());
        EXPECT_EQ("color:red", attrs.at("style"));
        EXPECT_EQ( "highlight", attrs.at("class"));
    }

    TEST_F(TagParsingTest, ParseInvalidNonTagInText) {
        // Input: m<n && m>a - invalid, no proper tag (name "n" not in factory)
        std::string input = "m<n && m>a </n>";

        // Use full parse
        wiki_parser = parser::Parser(input);
        auto elements = wiki_parser.parse();

        // Expect TextElement or invalid Tag (adjust based on lexer behavior)
        ASSERT_EQ(elements.size(), 1);  // Whole as text or invalid tag
        auto tag = dynamic_cast<elements::Tag*>(elements[0].get());
        if (tag) {
            EXPECT_TRUE(tag->is_invalid());
            EXPECT_EQ("n", tag->get_name());
        } else {
            // If treated as text
            auto text = dynamic_cast<elements::TextElement*>(elements[0].get());
            ASSERT_NE(nullptr, text);
            EXPECT_EQ(input, text->get_text());
        }
    }

    TEST_F(TagParsingTest, ParseValidTagInText) {
        // Input: m<span && m>a - valid tag <span> (name "span" in factory)
        std::string input = "m<span && m>a </span>";

        // Use full parse
        wiki_parser = parser::Parser(input);
        auto elements = wiki_parser.parse();

        // Expect 3 elements: text "m", Tag "span", text " && m>a"
        ASSERT_EQ(3, elements.size());

        // First: text "m"
        auto text1 = dynamic_cast<elements::TextElement*>(elements[0].get());
        ASSERT_NE(nullptr, text1);
        EXPECT_EQ("m", text1->get_text());

        // Second: valid opening Tag "span"
        auto tag = dynamic_cast<elements::Tag*>(elements[1].get());
        ASSERT_NE(nullptr ,tag);
        EXPECT_EQ("span", tag->get_name());
        EXPECT_TRUE(tag->is_opening());
        EXPECT_TRUE(tag->is_valid());
        EXPECT_TRUE(tag->get_attributes().empty());

        // Third: text " && m>a"
        auto text2 = dynamic_cast<elements::TextElement*>(elements[2].get());
        ASSERT_NE(nullptr, text2);
        EXPECT_EQ( "a", text2->get_text());
    }
}