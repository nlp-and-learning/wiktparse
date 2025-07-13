// tests/test_external_link_parsing.cpp
#include <gtest/gtest.h>
#include "../src1/parser/Parser.h"
#include "../src1/elements/ExternalLink.h"

namespace parser {
    // Fixture for ExternalLink tests
    class ExternalLinkParsingTest : public ::testing::Test {
    protected:
        parser::Parser wiki_parser{""};  // Initialize with empty input
    };

    TEST_F(ExternalLinkParsingTest, ParseSimpleExternalLinkNoSpace) {
        // Input: [https://en.wikipedia.org] - only the link without spaces
        std::string input = "[https://en.wikipedia.org]";

        // Call the subparser for external link
        size_t pos = 0;
        auto link_obj = wiki_parser.parse_external_link(input, pos);

        // Check if the object is not null
        ASSERT_NE(link_obj, nullptr);

        // Check the URL and display (no display)
        EXPECT_EQ(link_obj->get_url(), "https://en.wikipedia.org");
        EXPECT_FALSE(link_obj->get_display().has_value());
    }

    TEST_F(ExternalLinkParsingTest, ParseExternalLinkWithSpaceAndDescription) {
        // Input: [https://en.wikipedia.org Wikipedia] - link, followed by a space, description, other spaces possible
        std::string input = "[https://en.wikipedia.org Wikipedia]";

        // Call the subparser for external link
        size_t pos = 0;
        auto link_obj = wiki_parser.parse_external_link(input, pos);

        // Sprawdź, czy obiekt nie jest null
        ASSERT_NE(link_obj, nullptr);

        // Sprawdź url i display (trim spacje)
        EXPECT_EQ(link_obj->get_url(), "https://en.wikipedia.org");
        ASSERT_TRUE(link_obj->get_display().has_value());
        EXPECT_EQ(link_obj->get_display().value(), "Wikipedia");
    }
}