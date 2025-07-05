#pragma once
#include "CompositeText.h"

class TextParser {
public:
    static std::unique_ptr<Fragment> parse(const std::string &text, size_t &pos) {
        auto textFragment = std::make_unique<TextFragment>(text);
        return textFragment;
    }
};
