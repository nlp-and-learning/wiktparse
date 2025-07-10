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
        Whitespace::skipWhitespace(text, pos);
        if (text.compare(pos, 2, "}}") == 0) {
            pos += 2;
            break;
        }
        if (text[pos] == '|') ++pos;

        Whitespace::skipWhitespace(text, pos);
        size_t eq = text.find('=', pos);
        size_t nextSep = text.find_first_of("|{\n", pos);

        std::optional<std::string> optKey;
        if (eq != std::string::npos && eq < nextSep) {
            size_t spacePos = eq;
            while (spacePos>0 && text[spacePos-1] == ' ') spacePos--;
            std::string key = text.substr(pos, spacePos - pos);
            pos = eq + 1;
            Whitespace::skipWhitespace(text, pos);
            optKey = key;
        } else {
            optKey = std::nullopt;
        }
        MarkupParser parser(text, pos);
        templ->parameters.emplace_back(optKey, parser.parse(true));
        pos = parser.getPos();
    }
    return templ;
}
