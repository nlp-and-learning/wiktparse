#include "HeaderTerms.h"

#include <algorithm>
#include <regex>

#include "../util/textUtils.h"

namespace {

std::string collapseSpaces(const std::string &text) {
    std::string out;
    bool space = false;
    for (char c : text) {
        if (c == ' ' || c == '\t') {
            space = !out.empty();
            continue;
        }
        if (space)
            out += ' ';
        space = false;
        out += c;
    }
    return out;
}

// Splits template inner text at top-level '|'
std::vector<std::string> splitParams(const std::string &inner) {
    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (size_t i = 0; i < inner.size(); i++) {
        if (inner.compare(i, 2, "{{") == 0 || inner.compare(i, 2, "[[") == 0) {
            depth++;
            current += inner.substr(i, 2);
            i++;
        } else if ((inner.compare(i, 2, "}}") == 0 || inner.compare(i, 2, "]]") == 0) && depth > 0) {
            depth--;
            current += inner.substr(i, 2);
            i++;
        } else if (inner[i] == '|' && depth == 0) {
            parts.push_back(current);
            current.clear();
        } else
            current += inner[i];
    }
    parts.push_back(current);
    return parts;
}

void tokenize(const std::string &text, HeaderTokens &tokens) {
    std::string outside;
    size_t pos = 0;
    while (pos < text.size()) {
        auto start = text.find("{{", pos);
        if (start == std::string::npos) {
            outside += text.substr(pos);
            break;
        }
        outside += text.substr(pos, start - pos) + " ";
        int depth = 0;
        size_t i = start;
        for (; i + 1 < text.size(); i++) {
            if (text.compare(i, 2, "{{") == 0) {
                depth++;
                i++;
            } else if (text.compare(i, 2, "}}") == 0) {
                depth--;
                i++;
                if (depth == 0)
                    break;
            }
        }
        if (depth != 0) { // unclosed template: keep as text
            outside += text.substr(start);
            break;
        }
        auto params = splitParams(text.substr(start + 2, i - 1 - (start + 2)));
        HeaderTokens::Template tpl;
        tpl.name = trim(params[0]);
        for (size_t p = 1; p < params.size(); p++) {
            if (params[p].find("{{") != std::string::npos) {
                tokenize(params[p], tokens);
                tpl.args.emplace_back();
            } else if (params[p].find('=') == std::string::npos)
                tpl.args.push_back(headerPlainText(params[p]));
        }
        if (!tpl.name.empty())
            tokens.templates.push_back(std::move(tpl));
        pos = i + 1;
    }
    if (auto plain = headerPlainText(outside); !plain.empty())
        tokens.texts.push_back(plain);
}

void appendUtf8(std::string &out, unsigned long cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// &nbsp; &amp; &#233; &#xE9; -> characters; &nbsp; becomes a plain space;
// unknown entities and zero-width ones are removed
std::string decodeEntities(const std::string &text) {
    static const std::regex entity(R"(&(#[0-9]+|#[xX][0-9a-fA-F]+|[a-zA-Z]+);)");
    std::string out;
    auto begin = std::sregex_iterator(text.begin(), text.end(), entity);
    size_t last = 0;
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        out += text.substr(last, it->position() - last);
        auto name = (*it)[1].str();
        if (name[0] == '#') {
            unsigned long cp = name[1] == 'x' || name[1] == 'X' ? std::stoul(name.substr(2), nullptr, 16)
                                                                : std::stoul(name.substr(1));
            if (cp == 0xA0)
                out += ' ';
            else if (cp != 0x200B && cp != 0x200C && cp != 0x200D && cp != 0xFFFD)
                appendUtf8(out, cp);
        } else if (name == "nbsp" || name == "ensp" || name == "emsp" || name == "thinsp")
            out += ' ';
        else if (name == "amp")
            out += '&';
        else if (name == "lt")
            out += '<';
        else if (name == "gt")
            out += '>';
        else if (name == "quot")
            out += '"';
        else if (name == "apos")
            out += '\'';
        else if (name == "ndash")
            out += "–";
        else if (name == "mdash")
            out += "—";
        last = it->position() + it->length();
    }
    out += text.substr(last);
    return out;
}

} // namespace

std::string headerPlainText(const std::string &text) {
    static const std::regex link(R"(\[\[(?:[^\]|]*\|)?([^\]]*)\]\])");
    static const std::regex tag(R"(<[^>]*>)");
    std::string out = std::regex_replace(text, link, "$1");
    out = std::regex_replace(out, tag, "");
    out = decodeEntities(out);
    out.erase(std::remove(out.begin(), out.end(), '\''), out.end());
    out = collapseSpaces(out);
    // Trim punctuation at both ends; multibyte characters compared as whole sequences
    const std::string ascii = " ,.;:()[]-*#=/|!?";
    const std::string dashes[] = {"–", "—", " "};
    bool changed = true;
    while (changed && !out.empty()) {
        changed = false;
        if (ascii.find(out.front()) != std::string::npos) {
            out.erase(0, 1);
            changed = true;
        } else if (ascii.find(out.back()) != std::string::npos) {
            out.pop_back();
            changed = true;
        }
        for (const auto &dash : dashes) {
            if (out.starts_with(dash)) {
                out.erase(0, dash.size());
                changed = true;
            }
            if (out.ends_with(dash)) {
                out.resize(out.size() - dash.size());
                changed = true;
            }
        }
    }
    return out;
}

HeaderTokens tokenizeHeader(const std::string &header) {
    HeaderTokens tokens;
    tokenize(header, tokens);
    return tokens;
}

bool looksLikeLanguageCode(const std::string &text) {
    static const std::regex code(R"([a-z]{2,3}(-[a-zA-Z0-9]{2,8})*)");
    return std::regex_match(text, code);
}
