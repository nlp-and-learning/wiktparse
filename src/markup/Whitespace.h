#pragma once
#include <cstring>
#include <string>

struct Whitespace {
    static bool isWikiSpace(char c) {
        return std::strchr(" \t\f\v", static_cast<unsigned char>(c)) != nullptr;
    }

    static size_t skipWhitespace(const std::string& text, size_t& pos) {
        auto start = pos;
        while (pos < text.size() && isWikiSpace(text[pos]))
            ++pos;
        return pos - start;
    }

    static bool isBreak(char c) {
        return c=='\r' || c=='\n';
    }

    static size_t skipLineBreaks(const std::string& text, size_t& pos) {
        size_t countCR = 0;
        size_t countLF = 0;
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
        auto countBreak = std::max(countCR, countLF);
        return countBreak;
    }

    static std::pair<size_t, size_t> skipWhiteBreaks(const std::string& text, size_t& pos) {
        size_t breakCounter = 0;
        size_t whiteCounter = 0;
        while(pos < text.size()) {
            if (isBreak(text[pos]))
                breakCounter += skipLineBreaks(text, pos);
            else if (isWikiSpace(text[pos]))
                whiteCounter += skipWhitespace(text, pos);
            else break;
        }
        return {breakCounter, whiteCounter};
    }

};
