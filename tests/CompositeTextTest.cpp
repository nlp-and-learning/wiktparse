#include <gtest/gtest.h>
#include "../src/wikitext/CompositeText.h"
#include "../src/wikitext/TextParser.h"

TEST(CommentsTest, PlainText) {
    std::string input = "This is plain text";
    std::string expected = "This is plain text";
    size_t pos = 0;
    auto fragment = TextParser::parse(input,pos);
    EXPECT_EQ(expected, fragment->displayText());
}