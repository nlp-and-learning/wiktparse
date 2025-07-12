#pragma once
#include <cassert>
#include <memory>
#include "Header.h"
#include "../BaseParser.h"

class HeaderParser: public BaseParser {
    size_t countLeft(size_t end) {
        size_t  locPos = pos;
        while (locPos < end && text[locPos] == '=')
            locPos++;
        return locPos - pos;
    }
    size_t countRight(size_t end) {
        size_t  locPos = end - 1;
        while (locPos >= pos && text[locPos] == '=')
            locPos--;
        return end - 1 - locPos;
    }

    static bool isOdd(size_t x) {
        return x % 2 != 0;
    }
public:
    HeaderParser(const std::string& text, size_t pos) : BaseParser(text, pos) {}
    std::unique_ptr<Header> parse() {
        auto header = std::make_unique<Header>();
        if (text[pos] != '=' || pos>0 && !Whitespace::isBreak(text[pos - 1]))
            return header;

        auto endPos = trimmedEolPos();
        auto countL = countLeft(endPos);
        auto countR = countRight(endPos);
        auto lineLen = endPos - pos;
        if (countL + countR < lineLen) {
            header->level = std::min(countL, countR);
            header->name = text.substr(pos + header->level, lineLen - 2*header->level);
        } else {
            assert(countL == lineLen);
            assert(countR == lineLen);
            if (lineLen < 3)
                return header;
            if (isOdd(lineLen))
                header->name = "=";
            else
                header->name = "==";
            header->level = (lineLen - header->name.size()) / 2;
        }
        pos = endPos;
        return header;
    }
};
