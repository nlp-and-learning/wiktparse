#include <gtest/gtest.h>
#include <vector>
#include "../src/markup/wikitext/HeaderParser.h"

TEST(HeaderTest, HeaderSize) {
    std::string input = "=header=";
    std::string expected = "header";

    HeaderParser parser(input, 0);
    auto header = parser.parse();
    EXPECT_EQ(expected, header->name);
    EXPECT_EQ(1, header->level);
}

TEST(HeaderTest, BeforeGtAfter) {
    std::string input = "===header=";
    std::string expected = "==header";

    HeaderParser parser(input, 0);
    auto header = parser.parse();
    EXPECT_EQ(expected, header->name);
    EXPECT_EQ(1, header->level);
}

TEST(HeaderTest, BeforeLtAfter) {
    std::string input = "=header===";
    std::string expected = "header==";

    HeaderParser parser(input, 0);
    auto header = parser.parse();
    EXPECT_EQ(expected, header->name);
    EXPECT_EQ(1, header->level);
}

TEST(HeaderTest, SpacesAfterIgnored) {
    std::string input = "==header==  ";
    std::string expected = "header";

    HeaderParser parser(input, 0);
    auto header = parser.parse();
    EXPECT_EQ(expected, header->name);
    EXPECT_EQ(2, header->level);
}

TEST(HeaderTest, TextAfterInvalid) {
    std::string input = "==header==  abc";
    std::string expected = "==header==  abc";

    HeaderParser parser(input, 0);
    auto header = parser.parse();
    EXPECT_EQ(expected, header->name);
    EXPECT_EQ(0, header->level);
}

TEST(HeaderTest, MustStartAtLineBegin) {
    std::string input = " ==header==";

    HeaderParser parser(input, 0);
    auto header = parser.parse();
    EXPECT_EQ("", header->name);
    EXPECT_EQ(0, header->level);
}

bool nextLine(const std::string &input, size_t &pos) {
    pos = input.find('\n', pos);
    if (pos == std::string::npos)
        return false;
    while (pos<input.size() && input[pos] == '\n')
        pos++;
    return pos<input.size();
}

TEST(HeaderTest, MultiLine) {
    std::string input = "=header 1=\n==header 2==\n===header 3===\n====header 4====\n";
    std::vector<std::string> expected = {"header 1", "header 2", "header 3", "header 4"};
    size_t pos = 0;
    int counter = 0;
    do {
        HeaderParser parser(input, pos);
        auto header = parser.parse();
        EXPECT_EQ(expected[counter], header->name);
        EXPECT_EQ(counter+1, header->level);
        counter++;
    } while (nextLine(input,pos));
}

TEST(HeaderTest, MustStartAtLineBegin1) {
    std::string input = "=header 1=\n ==header 2==\n===header 3===\n ===header 4====\n";
    std::vector<std::string> expected = {"header 1", "", "header 3", ""};
    size_t pos = 0;
    int counter = 0;
    do {
        HeaderParser parser(input, pos);
        auto header = parser.parse();
        EXPECT_EQ(expected[counter], header->name);
        EXPECT_EQ(counter % 2 == 0?counter+1:0, header->level);
        counter++;
    } while (nextLine(input,pos));
}

TEST(HeaderTest, EqCanBeInside) {
    std::string input = "=header==header=";
    std::string expected = "header==header";

    HeaderParser parser(input, 0);
    auto header = parser.parse();
    EXPECT_EQ(expected, header->name);
    EXPECT_EQ(1, header->level);
}

TEST(HeaderTest, AloneEq) {
    std::vector<std::string> inputs =
        {"", "=", "==", "===", "====", "=====","======" };
    std::vector<std::string> expecteds =
        {"", "", "", "=", "==", "=","==" };
    EXPECT_EQ(expecteds.size(), inputs.size());
    for (int i = 0; i < expecteds.size(); i++) {
        HeaderParser parser(inputs[i], 0);
        auto header = parser.parse();
        EXPECT_EQ(expecteds[i], header->name);
        EXPECT_EQ(expecteds[i].empty()?0:(inputs[i].size() - expecteds[i].size())/2, header->level)  << "Mismatch at index " << i;
    }
}
