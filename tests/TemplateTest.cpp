#include <fstream>
#include <gtest/gtest.h>
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
    TemplateParser parser(input, 0);
    auto templ = parser.parse();
    EXPECT_EQ(input, templ->formatStr());
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
    TemplateParser parser(input, 0);
    auto templ = parser.parse();
    EXPECT_EQ(input, templ->formatStr());
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

TEST(TemplateParserTest, WikiLinkInTemplate) {
    std::string input = "{{chset-cell1 | 61 U+003D: EQUALS SIGN | [[=]] }}";
    TemplateParser parser(input, 0);
    auto templ = parser.parse();
    EXPECT_EQ(input, templ->dump());
}

TEST(TemplateParserTest, InvalidWikilink) {
    std::string input = "{{chset-cell1 | 61 U+003D: EQUALS SIGN | ]] }}";
    TemplateParser parser(input, 0);
    auto templ = parser.parse();
    EXPECT_EQ(input, templ->dump());
}

TEST(TemplateParserTest, Complex) {
    std::string input = R"({{quote|Prove that <math>\left[ \frac{1}{2} (n^2 + n) \right] T_6 + \left[ \frac{1}{2} (n^2 + 3n) \right] T_5 + (n + 1)T_4 + T_1 + T_2 + T_3 + T_7 \le cn^2,\ n \ge n_0</math>

: <math>\begin{align}
&\left[ \frac{1}{2} (n^2 + n) \right] T_6 + \left[ \frac{1}{2} (n^2 + 3n) \right] T_5 + (n + 1)T_4 + T_1 + T_2 + T_3 + T_7\\
\le {} &( n^2 + n )T_6 + ( n^2 + 3n )T_5 + (n + 1)T_4 + T_1 + T_2 + T_3 + T_7 \ (\text{for } n \ge 0 )
\end{align}</math>

Let ''k'' be a constant greater than or equal to [''T''<sub>1</sub>..''T''<sub>7</sub>]
<br /><br />
<math>\begin{align}
&T_6( n^2 + n ) + T_5( n^2 + 3n ) + (n + 1)T_4 + T_1 + T_2 + T_3 + T_7 \le k( n^2 + n ) + k( n^2 + 3n ) + kn + 5k\\
= {} &2kn^2 + 5kn + 5k \le 2kn^2 + 5kn^2 + 5kn^2 \ (\text{for } n \ge 1) = 12kn^2
\end{align}</math>
<br /><br />
Therefore <math>\left[ \frac{1}{2} (n^2 + n) \right] T_6 + \left[ \frac{1}{2} (n^2 + 3n) \right] T_5 + (n + 1)T_4 + T_1 + T_2 + T_3 + T_7 \le cn^2, n \ge n_0 \text{ for } c = 12k, n_0 = 1</math>
}})";
    TemplateParser parser(input, 0);
    auto templ = parser.parse();
    EXPECT_EQ(input, templ->dump());
}

TEST(TemplateParserTest, ComplexInvalid) {
    std::string input = R"({{n+1\atop m+1} \right\} = 0\quad(n>1 \text{ is odd})</math>

as a ''sum of integers'', which is not trivial. Here a combinatorial fact comes to surface which explains the vanishing of the Bernoulli numbers at odd index. Let {{math|''S''<sub>''n'',''m''</sub>}} be the number of surjective maps from {{math|1={1, 2, ..., ''n''}}} to {{math|1={1, 2, ..., ''m''}}}, then {{math|''S''<sub>''n'',''m''</sub> {{=}} ''m''!<big><big>{</big></big>{{su|p=''n''|b=''m''|a=c}}<big><big>}</big></big>}}. The last equation can only hold if

: <math> \sum_{\text{odd }m=1}^{n-1} \frac 2 {m^2}S_{n,m}=\sum_{\text{even } m=2}^n \frac{2}{m^2} S_{n,m} \quad (n>2 \text{ is even}). </math>

This equation can be proved by induction. The first two examples of this equation are

:{{math|1=''n'' = 4: 2 + 8 = 7 + 3}},
:{{math|1=''n'' = 6: 2 + 120 + 144 = 31 + 195 + 40}}.

Thus the Bernoulli numbers vanish at odd index because some non-obvious combinatorial identities are embodied in the Bernoulli numbers.

=== A restatement of the Riemann hypothesis ===
The connection between the Bernoulli numbers and the Riemann zeta function is strong enough to provide an alternate formulation of the [[Riemann hypothesis]] (RH) which uses only the Bernoulli numbers. In fact [[Marcel Riesz]] proved that the RH is equivalent to the following assertion:{{r|Riesz1916}}

:For every {{math|''ε'' > {{sfrac|1|4}}}} there exists a constant {{math|''C''<sub>''ε''</sub> > 0}} (depending on {{math|''ε''}}) such that {{math|{{abs|''R''(''x'')}} < ''C''<sub>''ε''</sub>''x''<sup>''ε''</sup>}} as {{math|''x'' → ∞}}.

Here {{math|''R''(''x'')}} is the [[Riesz function]]

: <math> R(x) = 2 \sum_{k=1}^\infty
\frac{k^{\overline{k}})";
    TemplateParser parser(input, 0);
    auto templ = parser.parse();
    EXPECT_TRUE(templ->invalid);
}