#include <gtest/gtest.h>

#include "../src/markup/Comments.h"
#include "../src/markup/MarkupParser.h"

TEST(CompositeTest, PlainText) {
    std::string input = "This is plain text";
    std::string expected = "This is plain text";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, CompactWhitespaces) {
    std::string input = "This\nis  \t plain  \t  text\n ";
    std::string expected = "This is plain text";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(input, fragment->dump());
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, Breaks1) {
    std::string input = "abc\ndef";
    std::string expected = "abc def";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, Breaks2) {
    std::string input = "abc\n\ndef";
    std::string expected = "abc\ndef";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, Breaks3) {
    std::string input = "abc\n\n\ndef";
    std::string expected = "abc\n\ndef";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, Breaks4) {
    std::string input = "abc\n\n\n\ndef";
    std::string expected = "abc\n\n\ndef";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, CommentBreakB) {
    std::string input = "a\n\n<!--comment -->\nb";
    std::string expected = "a\nb";
    std::string preparsed = Comments::clean(input);
    MarkupParser parser(preparsed, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(preparsed, fragment->dump());
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, CommentBreakC) {
    std::string input = "a\n<!--comment -->\n\nb";
    std::string expected = "a\nb";
    std::string preparsed = Comments::clean(input);
    MarkupParser parser(preparsed, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(preparsed, fragment->dump());
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CompositeTest, CommentBreakD) {
    std::string input = "a\n<!-- -->\n<!-- -->\nb";
    std::string expected = "a b";
    std::string preparsed = Comments::clean(input);
    MarkupParser parser(preparsed, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(preparsed, fragment->dump());
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, InvalidTag) {
    std::string input = "This is\n <!!> plain text";
    std::string expected = "This is <!!> plain text";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, BreakTag) {
    std::string input = "This is <br /> breaked text";
    std::string expected = "This is\nbreaked text";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, BreakTag1) {
    std::string input = "This is <br> breaked </br>text";
    std::string expected = "This is\nbreaked\ntext";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, SpanTag) {
    std::string input = "<span style=color:red class=highlight>abc</span>";
    std::string expected = "abc";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, SpanTag1) {
    std::string input = "<span style=color:red class=highlight>abc";
    std::string expected = "abc";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, UsualLt) {
    std::string input = "m<n && m>a";
    std::string expected = "m<n && m>a";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, LtWithName) {
    std::string input = "m<span && m>a";
    std::string expected = "ma";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(TaggedTextTest, LtWithName1) {
    std::string input = "m<span && m>a\n";
    std::string expected = "ma";
    MarkupParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}


TEST(CompositeTest, CompositeWithMultipleLinks) {
    std::string input = "[[France]], [[Belgium]] and [[Canada]]";
    MarkupParser parser(input, 0);
    auto composite = parser.parse(CalledFrom::Top);
    EXPECT_EQ("[[France]], [[Belgium]] and [[Canada]]", composite->dump());
}
