#include "TemplateParser.h"

#include <sstream>
#include <string>

#include "Template.h"

void TemplateParser::skipWhitespace(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(text[pos])) ++pos;
}

ParserFunction TemplateParser::parseParserFunction(const std::string& text, size_t& pos) {
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

    ParserFunction func;
    func.functionName = fname.str();

    while (pos < text.size()) {
        if (text.compare(pos, 2, "}}") == 0) {
            pos += 2;
            break;
        }
        if (text[pos] == '|') ++pos;
        func.arguments.push_back(parseTemplateValue(text, pos));
    }

    return func;
}

Template TemplateParser::parseTemplate(const std::string& text, size_t& pos) {
    if (text.compare(pos, 2, "{{") != 0)
        throw std::runtime_error("Expected {{ at pos " + std::to_string(pos));
    pos += 2;
    skipWhitespace(text, pos);

    std::ostringstream name;
    while (pos < text.size() && text[pos] != '|' && text.compare(pos, 2, "}}") != 0)
        name << text[pos++];

    Template tmpl;
    tmpl.name = name.str();

    while (pos < text.size()) {
        skipWhitespace(text, pos);
        if (text.compare(pos, 2, "}}") == 0) {
            pos += 2;
            break;
        }
        if (text[pos] == '|') ++pos;

        size_t eq = text.find('=', pos);
        size_t nextSep = text.find_first_of("|{", pos);

        if (eq != std::string::npos && eq < nextSep) {
            std::string key = text.substr(pos, eq - pos);
            pos = eq + 1;
            skipWhitespace(text, pos);
            tmpl.parameters.emplace_back(key, parseTemplateValue(text, pos));
        } else {
            tmpl.parameters.emplace_back(std::nullopt, parseTemplateValue(text, pos));
        }
    }
    return tmpl;
}

std::unique_ptr<TemplateValue> TemplateParser::parseTemplateValue(const std::string& text, size_t& pos) {
    if (text.compare(pos, 3, "{{#") == 0) {
        return std::make_unique<ParserFunction>(parseParserFunction(text, pos));
    }
    if (text.compare(pos, 2, "{{") == 0) {
        return std::make_unique<Template>(parseTemplate(text, pos));
    }

    std::ostringstream val;
    while (pos < text.size()) {
        if (text.compare(pos, 2, "{{") == 0 || text.compare(pos, 2, "}}") == 0 || text[pos] == '|')
            break;
        val << text[pos++];
    }
    return std::make_unique<RawText>(val.str());
}
