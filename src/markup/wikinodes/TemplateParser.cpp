#include "TemplateParser.h"
#include "../MarkupParser.h"

std::string TemplateParser::parseName() {
    std::ostringstream name;
    while (pos < text.size() &&
           text[pos] != '|' &&
           text[pos] != '\n' &&
           text.compare(pos, 2, "}}") != 0)
    {
        name << text[pos++];
    }
    return name.str();
}

std::unique_ptr<Template> TemplateParser::parse() {
    if (!startsWithFrom("{{")) return nullptr;
    size_t startPos = pos;
    pos += 2;
    Whitespace::skipWhitespace(text, pos);
    auto templ = std::make_unique<Template>();
    templ->name = parseName();
    while (pos < text.size()) {
        Whitespace::skipWhiteBreaks(text, pos);
        if (text.compare(pos, 2, "}}") == 0) {
            pos += 2;
            break;
        }
        if (text[pos] == '|') ++pos;

        size_t eq_nextSep = text.find_first_of("|{=", pos);
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
        templ->parameters.emplace_back(optKey, parser.parse(optKey?2:1));
        pos = parser.getPos();
    }
    return templ;
}
