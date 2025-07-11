#include <fstream>
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
    std::string input = R"({{Infobox country
| conventional_long_name = Republic of Poland
| common_name            = Poland
| native_name            = {{nativename|pl|Rzeczpospolita Polska}}
| image_flag             = Flag of Poland.svg
| flag_border            = Flag of Poland (normative).svgize
| image_coat             = Herb Polski.svg
}})";
    TemplateParser parser(input, 0);
    auto templ = parser.parse();
    EXPECT_EQ(input, templ->formatStr());
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
    auto templ = parser.parse();
    EXPECT_EQ(input, templ->formatStr());
}


TEST(TemplateParserTest, UnnamedParams) {
    std::string input = R"({{Chess diagram|tright
|Moves of a bishop
|  |  |  |  |  |  |  |oo
|oo|  |  |  |  |  |oo|
|  |oo|  |  |  |oo|  |
|  |  |oo|  |oo|  |  |
|  |  |  |bl|  |  |  |
|  |  |oo|  |oo|  |  |
|  |oo|  |  |  |oo|  |
|oo|  |  |  |  |  |oo|
}})";
    TemplateParser parser(input, 0);
    auto templ = parser.parse();
    EXPECT_EQ(input, templ->dump());
}
