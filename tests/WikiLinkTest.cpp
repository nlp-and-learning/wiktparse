#include <gtest/gtest.h>
#include "../src/markup/wikinodes/WikiLinkParser.h"

TEST(WikiLinkTest, Simple) {
    std::string input = "[[abc|def]]";
    WikiLinkParser parser(input, 0);
    auto wikiLink = parser.parse();
    EXPECT_EQ("abc", wikiLink->target);
    EXPECT_EQ("def", wikiLink->display);
}

TEST(WikiLinkTest, WikiLinkSimple) {
    std::string input = "[[France]]";
    WikiLinkParser parser(input, 0);
    auto wikiLink = parser.parse();
    EXPECT_EQ("[[France]]", wikiLink->dump());
}

TEST(WikiLinkTest, WikiLinkWithAlias) {
    std::string input = "[[France|French Republic]]";
    WikiLinkParser parser(input, 0);
    auto wikiLink = parser.parse();
    EXPECT_EQ("[[France|French Republic]]", wikiLink->dump());
}
