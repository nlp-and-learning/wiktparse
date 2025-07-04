#pragma once
#include <string>
#include "TemplateParser.h"

inline std::string parseAsCompact(const std::string& input) {
    size_t pos = 0;
    auto result = TemplateParser::parseTemplate(input, pos);
    return result.toWikitext(FormatStyle::Compact);
}

inline std::string parseAsMultiline(const std::string& input) {
    size_t pos = 0;
    auto result = TemplateParser::parseTemplate(input, pos);
    return result.toWikitext(FormatStyle::Multiline);
}
