#include <gtest/gtest.h>
#include "../src/wikitext/Tag.h"
#include "../src/wikitext/TagParser.h"

TEST(TagTest, SelfClosing) {
    std::string input = "<br  />";
    TagParser parser(input, 0);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::SelfClosing, tag->type);
    EXPECT_EQ("br", tag->name);
    EXPECT_EQ(0, tag->attributes.size());
}

TEST(TagTest, Attributes) {
    std::string input = R"(<span style="color:red;" class="highlight" title="example">)";
    TagParser parser(input, 0);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::Open, tag->type);
    EXPECT_EQ("span", tag->name);
    EXPECT_EQ(3, tag->attributes.size());
    EXPECT_EQ("example", tag->attributes[2].second);
}


TEST(TagTest, CommentStart) {
    std::string input = "<!--";
    size_t pos = 0;
    EXPECT_EQ(true, TagParser::startComment(input,pos));
}

TEST(TagTest, CommentAsInvalidTag) {
    std::string input = "<!--";
    TagParser parser(input, 0);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::Invalid, tag->type);
}