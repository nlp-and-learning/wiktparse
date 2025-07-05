#pragma once
#include "CompositeText.h"


enum class StartFragment {
    Plain,
    Comment,
    Tag,
    Function,
    Template,
    WikiLink,
};

class TextParser {
    size_t lineCount = 0;
    size_t currentLineStart = 0;
    const std::string& text;
    size_t pos;
public:
    explicit TextParser(const std::string& text, size_t pos) : text(text),pos(pos) {}

    void skipLineBreaks() {
        int countCR = 0;
        int countLF = 0;
        while (pos < text.size()) {
            if (text[pos] == '\r') {
                ++countCR;
                ++pos;
            } else if (text[pos] == '\n') {
                ++countLF;
                ++pos;
            } else {
                break;
            }
        }
        currentLineStart = pos;
        lineCount += std::max(countCR, countLF);
    }

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
