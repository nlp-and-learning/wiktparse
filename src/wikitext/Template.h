#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "text.h"

struct ParserFunction: TextFragment {
    std::string functionName;
    std::vector<std::unique_ptr<CompositeText>> arguments;
    [[nodiscard]] std::string toWikitext(FormatStyle style) const;
};

struct TemplateParameter {
    std::optional<std::string> name; // nullopt = pozycyjny
    std::unique_ptr<CompositeText> value;
    TemplateParameter(std::optional<std::string> name, std::unique_ptr<CompositeText> value)
        : name(std::move(name)), value(std::move(value)) {}
    std::string toWikitext(FormatStyle style) const;
};

struct Template: TextFragment {
    std::string name;
    std::vector<TemplateParameter> parameters;
    std::string toWikitext(FormatStyle style) const;
};
