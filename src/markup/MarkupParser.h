#pragma once
#include <memory>
#include "BaseParser.h"
#include "Markup.h"

enum class CalledFrom {Top, UnnamedParam, NamedParam, WikiLink};

class MarkupParser: public BaseParser {
public:
    MarkupParser(const std::string& text, size_t pos) : BaseParser(text, pos) {}
    std::unique_ptr<Markup> parse(CalledFrom asParamValue = CalledFrom::Top);
};
