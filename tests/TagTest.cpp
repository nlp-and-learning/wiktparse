#include <gtest/gtest.h>
#include "../src/wikitext/Tag.h"
#include "../src/wikitext/TagParser.h"

TEST(TagTest, SelfClosing) {
    std::string input = "<br  />";
    size_t pos = 0;
    auto tag = TagParser::parse(input,pos);
    EXPECT_EQ(TagType::SelfClosing, tag->type);
    EXPECT_EQ("br", tag->name);
    EXPECT_EQ(0, tag->attributes.size());
}
