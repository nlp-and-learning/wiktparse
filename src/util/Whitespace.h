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
};
