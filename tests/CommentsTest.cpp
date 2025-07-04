#include <gtest/gtest.h>
#include "../src/xml/Comments.h"

TEST(CommentsTest, NoWiki) {
    std::string input = "<nowiki>abc</nowiki>";
    std::string expected = "<nowiki>abc</nowiki>";
    EXPECT_EQ(expected, Comments::clean(input));
}