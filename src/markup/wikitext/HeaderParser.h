#pragma once
#include "../BaseParser.h"

class HeaderParser: public BaseParser {
public:
    HeaderParser(const std::string& text, size_t pos) : BaseParser(text, pos) {}
};
