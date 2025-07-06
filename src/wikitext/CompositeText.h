#pragma once
#include <memory>
#include <vector>
#include "../tags/Tag.h"

enum class FontStyle {
    Proportional,
    Monospaced,
    Latex,
};

struct Fragment {
    virtual ~Fragment() = default;
    [[nodiscard]] virtual std::string displayText() const = 0;
    [[nodiscard]] virtual std::string dump() const = 0;
};

struct TextFragment: public Fragment {
    std::string text;
    FontStyle fontStyle = FontStyle::Proportional;
    int fontSize = 0; //0: normal, 1 - big, -1 small, 2-bigger etc
    CommentStatus commentStatus;
    TextFragment(std::string t, CommentStatus commentStatus)
        : text(std::move(t)), commentStatus(commentStatus) {}
    [[nodiscard]] std::string displayText() const override {
        if (commentStatus == CommentStatus::Displayed)
            return text;
        else
            return "";
    };

    [[nodiscard]] std::string dump() const override {
        return text;
    };
};


struct CompositeText: public Fragment {
    std::vector<std::unique_ptr<Fragment>> parts;
    [[nodiscard]] std::string displayText() const override {
        std::ostringstream ss;
        for (auto &framgent: parts)
            ss << framgent->displayText();
        return ss.str();
    };
    [[nodiscard]] std::string dump() const override {
        std::ostringstream ss;
        for (auto &framgent: parts)
            ss << framgent->dump();
        return ss.str();
    };
};
