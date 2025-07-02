#include "Template.h"
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <iostream>

std::string ParserFunction::toWikitext(FormatStyle style) const {
    std::string out = "{{#" + functionName + ":";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) out += "|";
        out += arguments[i]->toWikitext(style);
    }
    out += "}}";
    return out;
}

std::string TemplateParameter::toWikitext(FormatStyle style) const {
    std::string s = "|";
    if (name) s += *name + "=";
    s += value->toWikitext();
    return s;
}

std::string Template::toWikitext(FormatStyle style) const {
    std::string out = "{{" + name;
    for (const auto& p : parameters)
        out += style == FormatStyle::Multiline ? ("\n" + p.toWikitext()) : p.toWikitext();
    out += style == FormatStyle::Multiline ? "\n}}" : "}}";
    return out;
}
