#include <gtest/gtest.h>
#include "../src/wikitext/CompositeText.h"
#include "../src/wikitext/TextParser.h"

TEST(CompositeTest, PlainText) {
    std::string input = "This is plain text";
    std::string expected = "This is plain text";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, CompactWhitespaces) {
    std::string input = "This\nis  \t plain  \t  text\n ";
    std::string expected = "This is plain text";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, InvalidTag) {
    std::string input = "This is\n <!!> plain text";
    std::string expected = "This is <!!> plain text";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, BreakTag) {
    std::string input = "This is <br /> breaked text";
    std::string expected = "This is\nbreaked text ";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}
