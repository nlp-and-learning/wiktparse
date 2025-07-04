#pragma once
#include "old_Template.h"

class old_TemplateParser {
    static void skipWhitespace(const std::string &text, size_t &pos);
public:
    static std::unique_ptr<old_ParserFunction> parseParserFunction(const std::string &text, size_t &pos);
    static std::string parseName(const std::string &text, size_t &pos);
    static std::unique_ptr<old_Template> parseTemplate(const std::string &text, size_t &pos);
};

