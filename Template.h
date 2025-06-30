#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>

enum class FormatStyle {
    Compact,   // {{name|a|b|c}}
    Multiline  // {{name\n|a\n|b\n|c\n}}
};

struct TemplateValue {
    [[nodiscard]] virtual std::string toWikitext(FormatStyle style = FormatStyle::Compact) const = 0;
    virtual ~TemplateValue() = default;
};

struct RawText : TemplateValue {
    std::string text;
    explicit RawText(std::string t) : text(std::move(t)) {}
    [[nodiscard]] std::string toWikitext(FormatStyle style = FormatStyle::Compact) const override { return text; }
};

struct ParserFunction : TemplateValue {
    std::string functionName;
    std::vector<std::unique_ptr<TemplateValue>> arguments;
    [[nodiscard]] std::string toWikitext(FormatStyle style = FormatStyle::Compact) const override;
};

struct TemplateParameter {
    std::optional<std::string> name; // nullopt = pozycyjny
    std::unique_ptr<TemplateValue> value;
    TemplateParameter(std::optional<std::string> name, std::unique_ptr<TemplateValue> value)
        : name(std::move(name)), value(std::move(value)) {}
    std::string toWikitext(FormatStyle style = FormatStyle::Compact) const;
};

struct Template : TemplateValue {
    std::string name;
    std::vector<TemplateParameter> parameters;
    std::string toWikitext(FormatStyle style = FormatStyle::Compact) const override;
};

std::unique_ptr<TemplateValue> parseTemplateValue(const std::string& text, size_t& pos);