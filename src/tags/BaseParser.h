#pragma once
#include <cstring>

enum class StartSpecial {
    CommentOpen,
    CommentClose,
    ConsiderTag,
    Function,
    Template,
    WikiLink,
    ExternalLink,
    Header,
    Other,
};

class BaseParser {
protected:
    size_t lineCount = 0;
    size_t currentLineStart = 0;
    const std::string& text;
    size_t pos;

    bool startsWithFrom(const std::string& prefix) {
        return text.size() >= pos + prefix.size() &&
               text.compare(pos, prefix.size(), prefix) == 0;
    }

    StartSpecial specialAt() {
        switch (text[pos]) {
            case '<':
            {
                if (startsWithFrom("<!--"))
                    return StartSpecial::CommentOpen;
                else
                    return StartSpecial::ConsiderTag;
            }
            case '-': {
                if (startsWithFrom("-->"))
                    return StartSpecial::CommentClose;
                else
                    return StartSpecial::Other;
            }
            case '{': {
                if (startsWithFrom("{{")) {
                    if (startsWithFrom("{{#"))
                        return StartSpecial::Function;
                    else
                        return StartSpecial::Template;
                } else
                    return StartSpecial::Other;
            }
            case '=': {
                if (pos == currentLineStart)
                    return StartSpecial::Header;
                else
                    return StartSpecial::Other;
            }
            case '[': {
                if (startsWithFrom("[["))
                    return StartSpecial::WikiLink;
                else
                    return StartSpecial::ExternalLink;
            }
            default: return StartSpecial::Other;
        }
    }

    static bool isWikiSpace(char c) {
        return std::strchr(" \t\f\v", static_cast<unsigned char>(c)) != nullptr;
    }
    static bool isBreak(char c) {
        return c=='\r' || c=='\n';
    }
    size_t skipWhitespace() {
        auto start = pos;
        while (pos < text.size() && isWikiSpace(text[pos]))
            ++pos;
        return pos - start;
    }
    size_t skipLineBreaks() {
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
        currentLineStart = pos;
        auto countBreak = std::max(countCR, countLF);
        lineCount += countBreak;
        return countBreak;
    }
    std::pair<size_t, size_t> skipWhiteBreaks() {
        size_t breakCounter = 0;
        size_t whiteCounter = 0;
        while(pos < text.size()) {
            if (isBreak(text[pos]))
                breakCounter += skipLineBreaks();
            else if (isWikiSpace(text[pos]))
                whiteCounter += skipWhitespace();
            else break;
        }
        return {breakCounter, whiteCounter};
    }
public:
    BaseParser(const std::string& text, size_t pos) : text(text),pos(pos) {}
};
