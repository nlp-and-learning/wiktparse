#pragma once
#include <cstring>

enum class StartFragment {
    Plain,
    Comment,
    Tag,
    Function,
    Template,
    WikiLink,
};

class Parser {
protected:
    size_t lineCount = 0;
    size_t currentLineStart = 0;
    const std::string& text;
    size_t pos;
    static bool isWikiSpace(char c) {
        return std::strchr(" \t\f\v", static_cast<unsigned char>(c)) != nullptr;
    }
    void skipWhitespace() {
        while (pos < text.size() && isWikiSpace(text[pos]))
            ++pos;
    }
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
public:
    Parser(const std::string& text, size_t pos) : text(text),pos(pos) {}
};
