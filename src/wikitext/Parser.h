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

class Parser {
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
