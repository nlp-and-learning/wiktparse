#pragma once
#include <sstream>

#include "Template.h"
#include "../BaseParser.h"

class TemplateParser: public BaseParser {
public:
    TemplateParser(const std::string& text, size_t pos) : BaseParser(text, pos) {}
    std::string parseName();
    std::unique_ptr<Template> parse();
};
