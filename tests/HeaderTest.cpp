#include <gtest/gtest.h>
#include <vector>
#include "../src/wikitext/CompositeText.h"
#include "../src/wikitext/TextParser.h"

TEST(HeaderTest, HeaderSize) {
    std::string input = "=header=";
    std::string expected = "header";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(HeaderTest, BeforeGtAfter) {
    std::string input = "===header=";
    std::string expected = "==header";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(HeaderTest, BeforeLtAfter) {
    std::string input = "=header===";
    std::string expected = "header==";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(HeaderTest, SpacesAfterIgnored) {
    std::string input = "==header==  ";
    std::string expected = "header";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(HeaderTest, TextAfterInvalid) {
    std::string input = "==header==  abc";
    std::string expected = "==header==  abc";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(HeaderTest, MustStartAtLineBegin) {
    std::string input = " ==header==";
    std::string expected = "==header==";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}


TEST(HeaderTest, MultiLine) {
    std::string input = "=header 1=\n==header 2==\n===header 3===\n===header 4====\n";
    std::string expected = "header 1\nheader 2\nheader 3\nheader 4\n";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(HeaderTest, MustStartAtLineBegin1) {
    std::string input = "=header 1=\n ==header 2==\n===header 3===\n ===header 4====\n";
    std::string expected = "header 1\nheader 2\nheader 3\nheader 4\n";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(HeaderTest, EqCanBeInside) {
    std::string input = "=header==header=";
    std::string expected = "header==header";
    TextParser parser(input, 0);
    auto fragment = parser.parse();
    EXPECT_EQ(expected, fragment->displayText());
}

TEST(HeaderTest, Alone) {
    std::vector<std::string> inputs =
        {"", "=", "==", "===", "====", "=====","======" };
    std::vector<std::string> expecteds =
        {"", "=", "==", "=", "==", "=","==" };
    EXPECT_EQ(expecteds.size(), inputs.size());
    for (size_t i = 0; i < expecteds.size(); ++i) {
        TextParser parser(inputs[i], 0);
        auto fragment = parser.parse();
        EXPECT_EQ(expecteds[i], fragment->displayText());
    }
}
