#pragma once
#include "Template.h"

class TemplateParser {
    static void skipWhitespace(const std::string &text, size_t &pos);
public:
    static std::unique_ptr<ParserFunction> parseParserFunction(const std::string &text, size_t &pos);
    static std::string parseName(const std::string &text, size_t &pos);
    static std::unique_ptr<Template> parseTemplate(const std::string &text, size_t &pos);
};

