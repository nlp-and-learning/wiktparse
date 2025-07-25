// tests/test_header_parsing.cpp
#include <gtest/gtest.h>
#include "../src1/parser/Parser.h"
#include "../src1/elements/Header.h"

namespace parser {
    // Fixture for header testing
    class HeaderParsingTest : public ::testing::Test {
    protected:
        parser::Parser wiki_parser{""};  // Initialize with empty input
    };

    TEST_F(HeaderParsingTest, ParseBalancedHeaderLevel3) {
        std::string input = "===123===";

        size_t pos = 0;
        auto header_obj = wiki_parser.parse_header(input, pos);

        ASSERT_NE(header_obj, nullptr);

        EXPECT_EQ(3, header_obj->get_level());
        EXPECT_EQ("123", header_obj->get_title());
    }

    TEST_F(HeaderParsingTest, ParseUnbalancedHeaderMinLevel) {
        // Input: "===abc==" (unbalanced, level=min(3,2)=2, title="=abc")
        std::string input = "===abc==";

        size_t pos = 0;
        auto header_obj = wiki_parser.parse_header(input, pos);

        ASSERT_NE(header_obj, nullptr);

        // Check the level (minimum on both sides) and title (contains an additional ‘=’)
        EXPECT_EQ(2, header_obj->get_level());
        EXPECT_EQ("=abc", header_obj->get_title());
    }
}