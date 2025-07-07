#include <gtest/gtest.h>
#include "../src/wikitext/CompositeText.h"
#include "../src/wikitext/TextParser.h"
#include "../src/tags/TagStreamParser.h"
#include "../src/markup/Comments.h"

TEST(CommentTest, HideComments) {
    std::string input = "This is<!--comment 1--> simple <!--comment 2-->text";
    std::string expected = "This is simple text";
    std::string comments = "<!--comment 1--><!--comment 2-->";

    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(CommentTest, CommentAtEnd) {
    std::string input = "This is simple text<!--comment";
    std::string expected = "This is simple text";

    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(CommentTest, CommentAtEnd2) {
    std::string input = "This is simple --> text<!--comment";
    std::string expected = "This is simple --> text";

    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(CommentTest, CommentInsideNowiki) {
    std::string input = "This is <nowiki><!--comment--></nowiki> text";
    std::string expected = "This is <!--comment--> text";

    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(CommentTest, CommentPartInsideNowiki) {
    std::string input = "This is <nowiki><!--comm</nowiki>ent--> text";
    std::string expected = "This is <!--comment--> text";

    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(CommentTest, NowikiInsideCommentPart) {
    std::string input = "This is <!--comm<nowiki>>ent--></nowiki text";
    std::string expected = "This is </nowiki text";

    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(CommentTest, NowikiInsideComment) {
    std::string input = "This is <!--comment <nowiki> abc <nowiki> --> text";
    std::string expected = "This is  text";

    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(CommentTest, CommentInsidePre) {
    std::string input = "This is <pre><!--comment--></pre> text";
    std::string expected = "This is <pre></pre> text";

    EXPECT_EQ(expected, Comments::clean(input));
}
