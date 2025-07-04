#include "old_Template.h"
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <iostream>

std::string old_ParserFunction::toWikitext(FormatStyle style) const {
    std::string out = "{{#" + functionName + ":";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) out += "|";
        out += arguments[i]->toWikitext(style);
    }
    out += "}}";
    return out;
}

std::string old_TemplateParameter::toWikitext(FormatStyle style) const {
    std::string s = "|";
    if (name) {
        if (style==FormatStyle::Multiline) {
            std::string indent(std::max(0, 17-int((*name).size())), ' ');
            s += ' ' + *name + indent + "= ";
        }
        else
            s += *name + "=";
    }
    s += value->toWikitext(FormatStyle::Compact);
    return s;
}

std::string old_Template::toWikitext(FormatStyle style) const {
    std::string out = "{{" + name;
    for (const auto& p : parameters)
        out += style == FormatStyle::Multiline ? ("\n" + p.toWikitext(style)) : p.toWikitext(style);
    out += style == FormatStyle::Multiline ? "\n}}" : "}}";
    return out;
}
