#pragma once
#include "CompositeText.h"

class TextParser {
public:
    static std::unique_ptr<Fragment> parse(const std::string &text, size_t &pos) {
        std::ostringstream ss;
        int spacesLen = 0;
        while (pos<text.size()) {
            unsigned char c = text[pos];
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
