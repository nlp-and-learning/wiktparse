#include <gtest/gtest.h>
#include "../src/markup/wikinodes/WikiLinkParser.h"

TEST(WikiLinkTest, Simple) {
    std::string input = "[[abc|def]]";
    WikiLinkParser parser(input, 0);
    auto wikiLink = parser.parse();
    EXPECT_EQ("abc", wikiLink->target);
    EXPECT_EQ(input, wikiLink->dump());
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

TEST(WikiLinkTest, WikilinkInWikilink) {
    std::string input = "[[File:Battle of Tannenberg.jpg|thumb|left|upright|The [[Battle of Grunwald]]";
    input += " was fought against the [[Teutonic Knights|German Order of Teutonic Knights]], and resulted";
    input += " in a decisive victory for the [[Kingdom of Poland (1385–1569)|Kingdom of Poland]], 15 July 1410.]]";
    WikiLinkParser parser(input, 0);
    auto wikiLink = parser.parse();
    EXPECT_EQ(input, wikiLink->dump());
}
