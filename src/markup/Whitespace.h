#pragma once
#include <cstring>
#include <sstream>
#include <string>

struct Whitespace {
    static bool isWikiSpace(char c) {
        return std::strchr(" \t\f\v", static_cast<unsigned char>(c)) != nullptr;
    }

    static bool isBreak(char c) {
        return c=='\r' || c=='\n';
    }

    static bool isBreakSpace(char c) {
        return isWikiSpace(c) || isBreak(c);
    }

    static size_t skipWhitespace(const std::string& text, size_t& pos) {
        auto start = pos;
        while (pos < text.size() && isWikiSpace(text[pos]))
            ++pos;
        return pos - start;
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

    static std::string compact(const std::string& text) {
        std::ostringstream ss;
        size_t pos = 0;
        while (pos < text.size()) {
            if (isBreakSpace(text[pos])) {
                auto p = skipWhiteBreaks(text, pos);
                if (pos < text.size()) {
                    if (p.first > 1) {
                        std::string breaks(p.first-1,'\n');
                        ss << breaks;
                    } else
                        ss << ' ';
                }
            }
            else {
                ss << text[pos];
                pos++;
            }
        }
        return ss.str();
    }
};
