#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "old_text.h"

struct old_TemplateParameter {
    std::optional<std::string> name; // nullopt = pozycyjny
    std::unique_ptr<old_TextFragment> value;
    old_TemplateParameter(std::optional<std::string> name, std::unique_ptr<old_TextFragment> value)
        : name(std::move(name)), value(std::move(value)) {}
    std::string toWikitext(old_FormatStyle style) const;
};

struct old_Template: old_TextFragment {
    std::string name;
    std::vector<old_TemplateParameter> parameters;
    std::string toWikitext(old_FormatStyle style) const;
};
