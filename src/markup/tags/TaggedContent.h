#pragma once
#include <memory>
#include "Tag.h"

class TaggedContent : public Markup {
public:
    std::unique_ptr<Tag> openTag;
    std::unique_ptr<Markups> content;  // Parsowany content między open a close
    std::unique_ptr<Tag> closeTag;     // Opcjonalny, dla walidacji
    TaggedContent(std::unique_ptr<Tag> open, std::unique_ptr<Markups> cont, std::unique_ptr<Tag> close = nullptr)
        : openTag(std::move(open)), content(std::move(cont)), closeTag(std::move(close)) {}
    [[nodiscard]] std::string dump() const override {
        std::ostringstream ss;
        ss << openTag->dump() << content->dump();
        if (closeTag) ss << closeTag->dump();
        return ss.str();
    }
    [[nodiscard]] std::string rawText() const override {
        return content->rawText();
    }
};
