#pragma once
#include <cstring>
#include "Whitespace.h"

enum class StartSpecial {
    Tag,
    Template,
    WikiLink,
    ExternalLink,
    Header,
    Other,
};

class BaseParser {
protected:
    const std::string& text;
    size_t pos;

    bool startsWithFrom(const std::string& prefix) {
        return text.size() >= pos + prefix.size() &&
               text.compare(pos, prefix.size(), prefix) == 0;
    }

    size_t trimmedEolPos() {
        auto posEol = text.find_first_of("\r\n", pos);
        if (posEol == std::string::npos)
            posEol = text.size();
        while (posEol>pos && Whitespace::isWikiSpace(text[posEol-1]))
            --posEol;
        return posEol;
    }

    StartSpecial specialAt() {
        switch (text[pos]) {
            case '<':
            {
                return StartSpecial::Tag;
            }
            case '{': {
                if (startsWithFrom("{{"))
                    return StartSpecial::Template;
                else
                    return StartSpecial::Other;
            }
            case '=': {
                if (pos == 0 || Whitespace::isBreak(text[pos - 1]))
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
public:
    BaseParser(const std::string& text, size_t pos) : text(text),pos(pos) {}
    [[nodiscard]] size_t getPos() const { return pos;}
};
