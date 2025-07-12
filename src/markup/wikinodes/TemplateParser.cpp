#include "TemplateParser.h"
#include "../MarkupParser.h"

bool TemplateParser::parseName(std::string &name) {
    std::ostringstream ss;
    while (pos < text.size() &&
           text[pos] != '|' &&
           text[pos] != '\n' &&
           !startsWithFrom("}}") &&
           !startsWithFrom("{{"))
    {
        ss << text[pos++];
    }
    if (text[pos] == '\n') {
        Whitespace::skipWhiteBreaks(text, pos);
        if (text[pos] != '|' && !startsWithFrom("}}"))
            return false;
    }
    if (startsWithFrom("{{"))
        return false;
    name = ss.str();
    return true;
}

std::unique_ptr<Template> TemplateParser::parse() {
    if (!startsWithFrom("{{")) return nullptr;
    size_t startPos = pos;
    pos += 2;
    Whitespace::skipWhitespace(text, pos);
    auto templ = std::make_unique<Template>();
    templ->invalid = !parseName(templ->name);
    if (templ->invalid)
        return templ;
    while (pos < text.size()) {
        Whitespace::skipWhiteBreaks(text, pos);
        if (text.compare(pos, 2, "}}") == 0) {
            pos += 2;
            break;
        }
        if (text[pos] == '|') ++pos;

        size_t eq_nextSep = text.find_first_of("|{[=", pos);
        std::optional<std::string> optKey;
        if (eq_nextSep != std::string::npos && text[eq_nextSep] == '=') {
            size_t spacePos = eq_nextSep;
            while (spacePos>0 && Whitespace::isWikiSpace(text[spacePos-1])) spacePos--;
            auto keyStart = pos;
            while (keyStart < spacePos && Whitespace::isWikiSpace(text[keyStart])) keyStart++;
            std::string key = text.substr(keyStart, spacePos - keyStart);
            pos = eq_nextSep + 1;
            optKey = key;
        } else {
            optKey = std::nullopt;
        }
        MarkupParser parser(text, pos);
        auto markup = parser.parse(optKey?CalledFrom::NamedParam:CalledFrom::UnnamedParam);
        if (markup) {
            templ->parameters.emplace_back(optKey, std::move(markup));
            pos = parser.getPos();
        } else {
            eq_nextSep = text.find_first_of("|{[", pos);
            if (eq_nextSep == std::string::npos)
                return templ;
            else
                pos = eq_nextSep;
        }
    }
    return templ;
}
