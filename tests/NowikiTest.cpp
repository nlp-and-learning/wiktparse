#include <gtest/gtest.h>
#include "../src/markup/Comments.h"

TEST(NowikiTest, CommentInsideNowiki) {
    std::string input = "<nowiki><!-- comment--></nowiki>";
    std::string expected = "<!-- comment-->";
    EXPECT_EQ(expected, Comments::preparse(input)->str());
}

TEST(NowikiTest, NowikiNotClosed) {
    std::string input = "<nowiki><!-- comment--><nowiki>";
    std::string expected = "<nowiki><nowiki>";
    EXPECT_EQ(expected, Comments::preparse(input)->str());
}

TEST(NowikiTest, NowikiSecondOpen) {
    std::string input = "<nowiki><!-- comment--><nowiki></nowiki>";
    std::string expected = "<!-- comment--><nowiki>";
    EXPECT_EQ(expected, Comments::preparse(input)->str());
}

TEST(NowikiTest, MultiNowiki) {
    std::string input = "abc<nowiki>a</nowiki><nowiki>a</nowiki>b<nowiki>c</nowiki><nowiki>d";
    std::string expected = "abcaabc<nowiki>d";
    EXPECT_EQ(expected, Comments::preparse(input)->str());
}

TEST(NowikiTest, StartPLusNowiki) {
    std::string input = "abc<nowiki>a</nowiki>";
    std::string expected = "abca";
    EXPECT_EQ(expected, Comments::preparse(input)->str());
}

TEST(NowikiTest, NotClosed) {
    std::string input = "abc<nowiki>a<nowiki>";
    std::string expected = "abc<nowiki>a<nowiki>";
    EXPECT_EQ(expected, Comments::preparse(input)->str());
}

TEST(NowikiTest, Templates) {
    std::string input = "{{name|a|b}}<nowiki>{{name|a|b}}</nowiki>";
    std::string expected = "{{name|a|b}}{{name|a|b}}";
    auto wikiText = Comments::preparse(input);
    auto wikiGroup = dynamic_cast<WikiGroup*>(wikiText.get());
    EXPECT_TRUE(wikiGroup);
    EXPECT_TRUE(wikiGroup->parts.size()==2);
    auto wikiFragment = dynamic_cast<WikiFragment*>(wikiGroup->parts[0].get());
    EXPECT_TRUE(wikiFragment->isActive);
    wikiFragment = dynamic_cast<WikiFragment*>(wikiGroup->parts[1].get());
    EXPECT_FALSE(wikiFragment->isActive);
    EXPECT_EQ(expected, wikiText->str());
}