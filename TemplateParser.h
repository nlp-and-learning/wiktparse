#pragma once
#include "Template.h"

class TemplateParser {
    static void skipWhitespace(const std::string &text, size_t &pos);
public:
    static ParserFunction parseParserFunction(const std::string &text, size_t &pos);
    static Template parseTemplate(const std::string &text, size_t &pos);
    static std::unique_ptr<TemplateValue> parseTemplateValue(const std::string &text, size_t &pos);
};

