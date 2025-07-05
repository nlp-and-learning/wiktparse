#pragma once
#include <memory>
#include <optional>
#include <string>
#include <sstream>
#include <vector>

enum class old_FormatStyle {
    Compact,   // {{name|a|b|c}}
    Multiline  // {{name\n|a\n|b\n|c\n}}
};

struct old_TextFragment {
    virtual ~old_TextFragment() = default;
    [[nodiscard]] virtual std::string toWikitext(old_FormatStyle style) const = 0;
};

struct old_PlainText : old_TextFragment {
    std::string text;
    explicit old_PlainText(std::string t) : text(std::move(t)) {}
    [[nodiscard]] std::string toWikitext(old_FormatStyle style) const override { return text; }
};

struct old_WikiLink : old_TextFragment {
    std::string target;
    std::vector<std::string> parameters;

    [[nodiscard]] std::string toWikitext(old_FormatStyle style) const override {
        std::string out = "[[" + target;
        for (const auto& p : parameters) {
            out += "|" + p;
        }
        out += "]]";
        return out;
    }
    [[nodiscard]] std::optional<std::string> getCaption() const {
        return parameters.empty() ? std::nullopt : std::make_optional(parameters.back());
    }
};

struct old_CompositeText : old_TextFragment {
    std::vector<std::unique_ptr<old_TextFragment>> parts;
    [[nodiscard]] std::string toWikitext(old_FormatStyle style) const override{
        std::string out;
        for (const auto& p : parts)
            out += p->toWikitext(style);
        return out;
    }
};

std::unique_ptr<old_WikiLink> old_parseWikiLink(const std::string& text, size_t& pos);
std::unique_ptr<old_TextFragment> old_parseCompositeText(const std::string& text, size_t& pos, bool insideParam=false);