#pragma once
#include <string>
#include <vector>

// Parts of a section header, e.g. "{{S|nom|fr|num=1}}" or "Etymology"
struct HeaderTokens {
    struct Template {
        std::string name;
        std::vector<std::string> args; // positional arguments as plain text; named ones skipped
    };
    std::vector<std::string> texts;     // plain text outside templates
    std::vector<Template> templates;    // including templates nested in arguments
};

// Plain text of wikitext without templates: [[a|b]] -> b, '' and tags removed,
// punctuation trimmed
std::string headerPlainText(const std::string &text);

HeaderTokens tokenizeHeader(const std::string &header);

// Looks like a language code: "fr", "sco", "roa-oca", "zh-Hant"
bool looksLikeLanguageCode(const std::string &text);
