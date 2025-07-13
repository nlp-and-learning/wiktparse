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

        auto header_obj = wiki_parser.parse_header(input);

        ASSERT_NE(header_obj, nullptr);

        EXPECT_EQ(header_obj->get_level(), 3);
        EXPECT_EQ(header_obj->get_title(), "123");
    }

    TEST_F(HeaderParsingTest, ParseUnbalancedHeaderMinLevel) {
        // Input: "===abc==" (unbalanced, level=min(3,2)=2, title="=abc")
        std::string input = "===abc==";

        auto header_obj = wiki_parser.parse_header(input);

        ASSERT_NE(header_obj, nullptr);

        // Check the level (minimum on both sides) and title (contains an additional ‘=’)
        EXPECT_EQ(header_obj->get_level(), 2);
        EXPECT_EQ(header_obj->get_title(), "=abc");
    }
}