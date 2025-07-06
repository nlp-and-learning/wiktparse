#include <gtest/gtest.h>
#include "../src/wikitext/CompositeText.h"
#include "../src/wikitext/TextParser.h"
#include "../src/tags/TagStreamParser.h"

TEST(CommentTest, HideComments) {
    std::string input = "This is<!--comment 1--> simple <!--comment 2-->text";
    std::string expected = "This is simple text";
    std::string comments = "<!--comment 1--><!--comment 2-->";
    TagStreamParser tagParser(input, 0);
    auto tagFragment = tagParser.parse();
    EXPECT_EQ(input, tagFragment->dump(DUMP_ALL));
    EXPECT_EQ(expected, tagFragment->dump(DUMP_DISPLAY));
    EXPECT_EQ(comments, tagFragment->dump(DUMP_COMMENT));
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CommentTest, CommentAtEnd) {
    std::string input = "This is simple --> text<!--comment";
    std::string expected = "This is simple --> text";
    TagStreamParser tagParser(input, 0);
    auto tagFragment = tagParser.parse();
    EXPECT_EQ(input, tagFragment->dump(DUMP_ALL));
    EXPECT_EQ(expected, tagFragment->dump(DUMP_DISPLAY));
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CommentTest, CommentInsideNowiki) {
    std::string input = "This is <nowiki><!--comment--></nowiki> text";
    std::string expected = "This is <!--comment--> text ";
    TagStreamParser tagParser(input, 0);
    auto tagFragment = tagParser.parse();
    EXPECT_EQ(input, tagFragment->dump(DUMP_ALL));
    EXPECT_EQ(expected, tagFragment->dump(DUMP_DISPLAY));
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CommentTest, CommentPartInsideNowiki) {
    std::string input = "This is <nowiki><!--comm</nowiki>ent--> text";
    std::string expected = "This is <!--comment--> text ";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CommentTest, NowikiInsideCommentPart) {
    std::string input = "This is <!--comm<nowiki>>ent--></nowiki text";
    std::string expected = "This is </nowiki text";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CommentTest, NowikiInsideComment) {
    std::string input = "This is <!--comment <nowiki> abc <nowiki> --> text";
    std::string expected = "This is text ";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(CommentTest, CommentInsidePre) {
    std::string input = "This is <pre><!--comment--></pre> text";
    std::string expected = " This is ";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}
