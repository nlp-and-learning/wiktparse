// tests/test_template_parsing.cpp
#include <gtest/gtest.h>
#include "../src1/parser/Parser.h"  // Główny parser

namespace parser {
    // Fixture z friend access (The name must match friend in Parser.h.)
    class TemplateParsingTest : public ::testing::Test {
    protected:
        parser::Parser wiki_parser{""};  // Initialize with empty input, then change
    };

    TEST_F(TemplateParsingTest, ParseSimpleTemplate) {
        // Set the input for the parser (or create a new object in the test)
        wiki_parser = parser::Parser{"{{name|param0|key1=param1}}"};

        // Call a private method directly (thanks to friend)
        size_t pos = 0;
        auto template_obj = wiki_parser.parse_template("{{name|param0|key1=param1}}", pos);

        // Check if the object is not null
        ASSERT_NE(template_obj, nullptr);

        // Check the template name
        EXPECT_EQ("name", template_obj->get_name());

        // Check the parameters
        const auto& params = template_obj->get_params();
        EXPECT_EQ(2, params.size());
        EXPECT_EQ("param0", params.at("1"));
        EXPECT_EQ("param1", params.at("key1"));
    }
}