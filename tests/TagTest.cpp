#include <gtest/gtest.h>
#include "../src/tags/Tag.h"
#include "../src/tags/TagParser.h"

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

TEST(TagTest, Attributes1) {
    std::string input = R"(<span style='color:red' class=highlight>)";
    TagParser parser(input, 0);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::Open, tag->type);
    EXPECT_EQ("span", tag->name);
    EXPECT_EQ(2, tag->attributes.size());
    EXPECT_EQ("class", tag->attributes[1].first);
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

TEST(TagTest, SpanTag) {
    std::string input = "<span style=color:red class=highlight>";
    TagParser parser(input, 0);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::Open, tag->type);
    EXPECT_EQ("span", tag->name);
}

TEST(TagTest, UsualLt) {
    std::string input = "m<n && m>a";
    TagParser parser(input, 1);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::Invalid, tag->type);
    EXPECT_EQ("n", tag->name);
}

TEST(TagTest, LtWithName) {
    std::string input = "m<span && m>a";
    TagParser parser(input, 1);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::Open, tag->type);
    EXPECT_EQ("span", tag->name);
}

TEST(TagTest, Badname) {
    std::string input = "<badname>";
    TagParser parser(input, 0);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::Invalid, tag->type);
    EXPECT_EQ("badname", tag->name);
    EXPECT_EQ(1, parser.getPos());
}

TEST(TagTest, Long) {
    std::string input = "<span long text with \n end text>";
    TagParser parser(input, 0);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::Open, tag->type);
    EXPECT_EQ("span", tag->name);
    EXPECT_EQ(input.size(), parser.getPos());
}

TEST(TagTest, NoGt) {
    std::string input = "<span long text with \n end text";
    TagParser parser(input, 0);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::Invalid, tag->type);
    EXPECT_EQ("span", tag->name);
    EXPECT_EQ(1, parser.getPos());
}

TEST(TagTest, OtherLt) {
    std::string input = "<span long text <with \n end text>>";
    TagParser parser(input, 0);
    auto tag = parser.parse();
    EXPECT_EQ(TagType::Invalid, tag->type);
    EXPECT_EQ("span", tag->name);
    EXPECT_EQ(1, parser.getPos());
}