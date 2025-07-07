#pragma once
#include "CompositeText.h"
#include "../markup/BaseParser.h"

class TextParser: public BaseParser {
public:
    TextParser(const std::string& text, size_t pos) : BaseParser(text, pos) {}
    std::unique_ptr<Fragment> parse() {
        auto composite = std::make_unique<CompositeText>();
        std::ostringstream ss;
        while (pos < text.size()) {
            auto spec = specialAt();
            ss << text[pos];
            pos++;
        }
        if (!ss.str().empty()) {
            auto rest = ss.str();
            composite->parts.emplace_back(std::make_unique<TextFragment>(rest));
        }
        if (composite->parts.empty())
            return std::make_unique<TextFragment>("");
        else if (composite->parts.size() == 1)
            return std::move(composite->parts[0]);
        else
            return composite;
    }
};
