#pragma once
#include "CompositeText.h"
#include "Parser.h"

class TextParser: public Parser {
public:
    TextParser(const std::string& text, size_t pos) : Parser(text, pos) {}
    std::unique_ptr<Fragment> parse() {
        auto composite = std::make_unique<CompositeText>();
        std::ostringstream ss;
        while (true) {
            auto p = skipWhiteBreaks();
            if (pos >= text.size()) break;
            if (p.first > 1) {
                std::string breaks(p.first-1, '\n');
                ss << breaks;
            } else if (p.first + p.second > 0)
                ss << ' ';
            unsigned char c = text[pos];
            //if (c == '<') - need specialAt
            ss << c;
            pos++;
        }
        auto textFragment = std::make_unique<TextFragment>(ss.str());
        return textFragment;
    }
};
