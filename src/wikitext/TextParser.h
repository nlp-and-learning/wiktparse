#pragma once
#include "CompositeText.h"
#include "Parser.h"

class TextParser: public Parser {
public:
    TextParser(const std::string& text, size_t pos) : Parser(text, pos) {}
    std::unique_ptr<Fragment> parse() {
        auto composite = std::make_unique<CompositeText>();
        std::ostringstream ss;
        int spacesLen = 0;
        while (pos < text.size()) {
            unsigned char c = text[pos];
            if (c == '<')
            if (isspace(c))
                spacesLen++;
            else {
                if (spacesLen>0) {
                    ss << ' ';
                    spacesLen = 0;
                }
                ss << c;
            }
            pos++;
        }
        auto textFragment = std::make_unique<TextFragment>(ss.str());
        return textFragment;
    }
};
