#pragma once
#include <memory>
#include <optional>
#include <string>
#include <sstream>
#include <vector>

enum class FormatStyle {
    Compact,   // {{name|a|b|c}}
    Multiline  // {{name\n|a\n|b\n|c\n}}
};

struct TextFragment {
    virtual ~TextFragment() = default;
    [[nodiscard]] virtual std::string toWikitext(FormatStyle style) const = 0;
};

struct PlainText : TextFragment {
    std::string text;
    explicit PlainText(std::string t) : text(std::move(t)) {}
    [[nodiscard]] std::string toWikitext(FormatStyle style) const override { return text; }
};

struct WikiLink : TextFragment {
    std::string target;
    std::optional<std::string> displayText;
    [[nodiscard]] std::string toWikitext(FormatStyle style) const override {
        std::string out = "[[" + target;
        if (displayText && *displayText != target)
            out += "|" + *displayText;
        out += "]]";
        return out;
    }
};

struct CompositeText {
    std::vector<std::unique_ptr<TextFragment>> parts;
    [[nodiscard]] std::string toWikitext(FormatStyle style) const {
        std::string out;
        for (const auto& p : parts)
            out += p->toWikitext(style);
        return out;
    }
};

std::unique_ptr<WikiLink> parseWikiLink(const std::string& text, size_t& pos);
std::unique_ptr<CompositeText> parseCompositeText(const std::string& text, size_t& pos, bool insideParam=false);