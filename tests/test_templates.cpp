#include <gtest/gtest.h>
#include "../src/wikitext/old_TemplateParser.h"
#include "../src/markup/wikinodes/TemplateParser.h"

std::string parseAndPrint(const std::string& input) {
    TemplateParser parser(input, 0);
    auto temp = parser.parse();
    return temp->dump();
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
    EXPECT_EQ(expected, parseAndPrint(input));
}

TEST(TemplateParserTest, Function) {
    std::string input = R"({{#invoke:Chart|pie chart
| slices =
    ( 82.42 : UK : blue )
| units suffix = _bill._AMD
| percent = true
}})";
    size_t pos = 0;
    auto result = old_parseCompositeText(input, pos, false);
}

TEST(TemplateParserTest, FunctionInsideTemplate) {
    std::string input = R"({{align|right|
{{#invoke:Chart|pie chart
| slices =
    ( 82.42 : UK : blue )
| units suffix = _bill._AMD
| percent = true
}}
}})";
    size_t pos = 0;
    auto result = old_parseCompositeText(input, pos, false);
}

TEST(TemplateParserTest, FormatStr1) {
    std::string input = R"({{Short description|Country in Central Europe}}
{{About|the country}}
{{pp-move}}
{{protection padlock|small=yes}}
{{Use dmy dates|date=July 2024}}
{{Use British English|date=June 2024}}
{{Infobox country
| conventional_long_name = Republic of Poland
| common_name            = Poland
| native_name            = {{nativename|pl|Rzeczpospolita Polska}}
| image_flag             = Flag of Poland.svg
| flag_border            = Flag of Poland (normative).svgize
| image_coat             = Herb Polski.svg
}})";
}



TEST(TemplateParserTest, FormatStr2) {
    std::string input = R"({{Geographic location
 | Center    = ''Cartagena''
 | North     = Caribbean Sea, Bocacanoa
 | Northeast = Bayunca, [[Clemencia, Colombia|Clemencia]]
 | East      = [[Villanueva, Bolívar]], [[San Estanislao]]
 | Southeast = [[Turbaco]], [[Arjona, Colombia|Arjona]]
 | South     = [[Tierra Bomba Island]], Portonao.
 | Southwest = Caribbean Sea
 | West      = Caribbean Sea
 | Northwest = Caribbean Sea
}})";
    TemplateParser parser(input, 0);
    auto temp = parser.parse();
    std::cout <<  temp->dump();
}
