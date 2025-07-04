#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "old_text.h"

struct old_ParserFunction: TextFragment {
    std::string functionName;
    std::vector<std::unique_ptr<TextFragment>> arguments;
    [[nodiscard]] std::string toWikitext(FormatStyle style) const;
};

struct old_TemplateParameter {
    std::optional<std::string> name; // nullopt = pozycyjny
    std::unique_ptr<TextFragment> value;
    old_TemplateParameter(std::optional<std::string> name, std::unique_ptr<TextFragment> value)
        : name(std::move(name)), value(std::move(value)) {}
    std::string toWikitext(FormatStyle style) const;
};

struct old_Template: TextFragment {
    std::string name;
    std::vector<old_TemplateParameter> parameters;
    std::string toWikitext(FormatStyle style) const;
};
