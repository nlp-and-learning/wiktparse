#pragma once
#include <memory>
#include <vector>

enum class FontStyle {
    Proportional,
    Monospaced,
    Latex,
};

struct Fragment {
    virtual ~Fragment() = default;
    [[nodiscard]] virtual std::string displayText() const = 0;
};

struct TextFragment: public Fragment {
    std::string text;
    FontStyle fontStyle = FontStyle::Proportional;
    int fontSize = 0; //0: normal, 1 - big, -1 small, 2-bigger etc
    explicit TextFragment(std::string t) : text(std::move(t)) {}
    [[nodiscard]] std::string displayText() const override {
        return text;
    };
};


struct CompositeTextTest: public Fragment {
    std::vector<std::unique_ptr<Fragment>> parts;
    [[nodiscard]] std::string displayText() const override {
        std::ostringstream ss;
        for (auto &framgent: parts)
            ss << framgent->displayText();
        return ss.str();
    };
};
