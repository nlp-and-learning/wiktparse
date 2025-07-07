#include "old_TemplateParser.h"

#include <sstream>
#include <string>

#include "old_Template.h"
#include "../util/textUtils.h"

void old_TemplateParser::skipWhitespace(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(text[pos])) ++pos;
}

std::string old_TemplateParser::parseName(const std::string &text, size_t &pos) {
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

 std::unique_ptr<old_Template>  old_TemplateParser::parseTemplate(const std::string& text, size_t& pos) {
    if (text.compare(pos, 2, "{{") != 0)
        throw std::runtime_error("Expected {{ at pos " + std::to_string(pos));
    pos += 2;
    skipWhitespace(text, pos);

    auto tmpl = std::make_unique<old_Template>();
    tmpl->name = parseName(text, pos);

    while (pos < text.size()) {
        skipWhitespace(text, pos);
        if (text.compare(pos, 2, "}}") == 0) {
            pos += 2;
            break;
        }
        if (text[pos] == '|') ++pos;

        skipWhitespace(text, pos);
        size_t eq = text.find('=', pos);
        size_t nextSep = text.find_first_of("|{\n", pos);

        std::optional<std::string> optKey;
        if (eq != std::string::npos && eq < nextSep) {
            size_t spacePos = eq;
            while (spacePos>0 && text[spacePos-1] == ' ') spacePos--;
            std::string key = text.substr(pos, spacePos - pos);
            pos = eq + 1;
            skipWhitespace(text, pos);
            optKey = key;
        } else {
            optKey = std::nullopt;
        }
        tmpl->parameters.emplace_back(optKey, old_parseCompositeText(text, pos, true));
    }
    return tmpl;
}