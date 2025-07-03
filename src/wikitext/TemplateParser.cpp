#include "TemplateParser.h"

#include <sstream>
#include <string>

#include "Template.h"
#include "../util/textUtils.h"

void TemplateParser::skipWhitespace(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(text[pos])) ++pos;
}

std::unique_ptr<ParserFunction>  TemplateParser::parseParserFunction(const std::string& text, size_t& pos) {
    if (text.compare(pos, 3, "{{#") != 0)
        throw std::runtime_error("Expected parser function at pos " + std::to_string(pos));
    pos += 3;
    skipWhitespace(text, pos);

    std::ostringstream fname;
    while (pos < text.size() && text[pos] != ':' && text.compare(pos, 2, "}}") != 0)
        fname << text[pos++];

    if (pos >= text.size() || text[pos] != ':')
        throw std::runtime_error("Missing ':' after parser function name");
    ++pos;

    auto func = std::make_unique<ParserFunction>();
    func->functionName = fname.str();
    while (pos < text.size()) {
        if (text.compare(pos, 2, "}}") == 0) {
            pos += 2;
            break;
        }
        if (text[pos] == '|') ++pos;
        func->arguments.push_back(parseCompositeText(text, pos, true));
    }
    return func;
}

 std::unique_ptr<Template>  TemplateParser::parseTemplate(const std::string& text, size_t& pos) {
    if (text.compare(pos, 2, "{{") != 0)
        throw std::runtime_error("Expected {{ at pos " + std::to_string(pos));
    pos += 2;
    skipWhitespace(text, pos);

    std::ostringstream name;
    while (pos < text.size() &&
       text[pos] != '|' &&
       text[pos] != '\n' &&
       text.compare(pos, 2, "}}") != 0)
    {
        name << text[pos++];
    }

    auto tmpl = std::make_unique<Template>();
    tmpl->name = name.str();

    while (pos < text.size()) {
        skipWhitespace(text, pos);
        if (text.compare(pos, 2, "}}") == 0) {
            pos += 2;
            break;
        }
        if (text[pos] == '|') ++pos;

        skipWhitespace(text, pos);
        size_t eq = text.find('=', pos);
        size_t nextSep = text.find_first_of("|{", pos);

        std::optional<std::string> optKey;;
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
        tmpl->parameters.emplace_back(optKey, parseCompositeText(text, pos, true));
    }
    return tmpl;
}