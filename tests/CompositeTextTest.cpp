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
    EXPECT_EQ(input, fragment->dump());
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, Breaks1) {
    std::string input = "abc\ndef";
    std::string expected = "abc def";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, Breaks2) {
    std::string input = "abc\n\ndef";
    std::string expected = "abc\ndef";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, Breaks3) {
    std::string input = "abc\n\n\ndef";
    std::string expected = "abc\n\ndef";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, Breaks4) {
    std::string input = "abc\n\n\n\ndef";
    std::string expected = "abc\n\n\ndef";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, CommentBreakA) {
    std::string input = "a\n<!--comment -->\nb";
    std::string expected = "a b";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(input, fragment->dump());
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, CommentBreakB) {
    std::string input = "a\n\n<!--comment -->\nb";
    std::string expected = "a\n\nb";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(input, fragment->dump());
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, CommentBreakC) {
    std::string input = "a\n<!--comment -->\n\nb";
    std::string expected = "a\n\nb";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(input, fragment->dump());
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, CommentBreakD) {
    std::string input = "a\n<!-- -->\n<!-- -->\nb";
    std::string expected = "a b";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(input, fragment->dump());
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
    std::string expected = "This is\nbreaked text";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, BreakTag1) {
    std::string input = "This is <br> breaked </br>text";
    std::string expected = "This is\nbreaked\ntext";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, SpanTag) {
    std::string input = "<span style=color:red class=highlight>abc</span>";
    std::string expected = "abc";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, SpanTag1) {
    std::string input = "<span style=color:red class=highlight>abc";
    std::string expected = "abc";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, UsualLt) {
    std::string input = "m<n && m>a";
    std::string expected = "m<n && m>a ";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, LtWithName) {
    std::string input = "m<span && m>a";
    std::string expected = "ma";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, LtWithName1) {
    std::string input = "m<span && m>a\n";
    std::string expected = "ma ";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}
