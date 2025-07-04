#include <gtest/gtest.h>
#include "../src/wikitext/old_TemplateParser.h"

// Pomocnik: zwraca wynik jako tekst w stylu zwiniętym
std::string parseAndPrint(const std::string& input) {
    size_t pos = 0;
    auto result = old_TemplateParser::parseTemplate(input, pos);
    return result->toWikitext(FormatStyle::Compact);
}

TEST(TemplateParserTest, BasicTemplate) {
    std::string input = "{{test|foo|bar=baz}}";
    std::string expected = "{{test|foo|bar=baz}}";
    EXPECT_EQ(expected, parseAndPrint(input));
}

TEST(TemplateParserTest, NestedTemplate) {
    std::string input = "{{outer|{{inner|val}}|key=value}}";
    std::string expected = "{{outer|{{inner|val}}|key=value}}";
    EXPECT_EQ(expected, parseAndPrint(input));
}

TEST(TemplateParserTest, ParserFunction) {
    std::string input = "{{#expr: 2+2}}";
    std::string expected = "{{#expr:2+2}}";
//    EXPECT_EQ(expected, parseAndPrint(input));
}

TEST(TemplateParserTest, WikiLinkSimple) {
    std::string input = "[[France]]";
    size_t pos = 0;
    auto result = parseWikiLink(input, pos);
    EXPECT_EQ("[[France]]", result->toWikitext(FormatStyle::Compact));
}

TEST(TemplateParserTest, WikiLinkWithAlias) {
    std::string input = "[[France|French Republic]]";
    size_t pos = 0;
    auto result = parseWikiLink(input, pos);
    EXPECT_EQ("[[France|French Republic]]", result->toWikitext(FormatStyle::Compact));
}

TEST(TemplateParserTest, CompositeWithMultipleLinks) {
    std::string input = "[[France]], [[Belgium]] and [[Canada]]";
    size_t pos = 0;
    auto result = parseCompositeText(input, pos, false);
    EXPECT_EQ("[[France]], [[Belgium]] and [[Canada]]", result->toWikitext(FormatStyle::Compact));
}
