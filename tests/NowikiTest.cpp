#include <gtest/gtest.h>
#include "../src/comments/Comments.h"

TEST(NowikiTest, CommentInsideNowiki) {
    std::string input = "<nowiki><!-- comment--></nowiki>";
    std::string expected = "<!-- comment-->";
    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(NowikiTest, NowikiNotClosed) {
    std::string input = "<nowiki><!-- comment--><nowiki>";
    std::string expected = "<nowiki><nowiki>";
    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(NowikiTest, NowikiSecondOpen) {
    std::string input = "<nowiki><!-- comment--><nowiki></nowiki>";
    std::string expected = "<!-- comment--><nowiki>";
    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(NowikiTest, MultiNowiki) {
    std::string input = "abc<nowiki>a</nowiki><nowiki>a</nowiki>b<nowiki>c</nowiki><nowiki>d";
    std::string expected = "abcaabc<nowiki>d";
    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(NowikiTest, StartPLusNowiki) {
    std::string input = "abc<nowiki>a</nowiki>";
    std::string expected = "abca";
    EXPECT_EQ(expected, Comments::clean(input));
}

TEST(NowikiTest, NotClosed) {
    std::string input = "abc<nowiki>a<nowiki>";
    std::string expected = "abc<nowiki>a<nowiki>";
    EXPECT_EQ(expected, Comments::clean(input));
}