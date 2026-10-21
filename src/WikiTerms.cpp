#include "WikiTerms.h"

#include <algorithm>
#include <fstream>

#include <wikilib/core/unicode_utils.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

void appendUtf8(std::string &out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

uint32_t hex4(std::string_view s, size_t pos) {
    if (pos + 4 > s.size())
        return 0xFFFD;
    uint32_t v = 0;
    for (size_t i = pos; i < pos + 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return 0xFFFD;
    }
    return v;
}

// End of a JSON string body starting at pos (index of closing quote)
size_t stringEnd(std::string_view text, size_t pos) {
    while (pos < text.size()) {
        if (text[pos] == '\\')
            pos += 2;
        else if (text[pos] == '"')
            return pos;
        else
            pos++;
    }
    return std::string_view::npos;
}

} // namespace

std::string WikiTerms::unescapeJson(std::string_view raw) {
    if (raw.find('\\') == std::string_view::npos)
        return std::string(raw);
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) {
            out += raw[i];
            continue;
        }
        char c = raw[++i];
        switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                uint32_t cp = hex4(raw, i + 1);
                i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < raw.size() && raw[i + 1] == '\\' && raw[i + 2] == 'u') {
                    uint32_t low = hex4(raw, i + 3);
                    if (low >= 0xDC00 && low < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i += 6;
                    }
                }
                appendUtf8(out, cp);
                break;
            }
            default: out += c; // \" \\ \/
        }
    }
    return out;
}

std::string WikiTerms::labelLanguage(const std::string &wiki) {
    if (wiki == "simple") return "en";
    if (wiki == "zh_min_nan") return "nan";
    if (wiki == "roa_rup") return "rup";
    if (wiki == "no") return "nb";
    std::string lang = wiki;
    std::ranges::replace(lang, '_', '-');
    return lang;
}

size_t WikiTerms::load(const fs::path &dir) {
    size_t count = 0;
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".txt")
            continue;
        auto &set = terms[labelLanguage(entry.path().stem().string())];
        std::ifstream in(entry.path());
        for (std::string line; std::getline(in, line);) {
            if (line.empty())
                continue;
            auto lower = wikilib::unicode::to_lower(line);
            maxBytes = std::max(maxBytes, lower.size());
            if (set.insert(std::move(lower)).second)
                count++;
        }
    }
    return count;
}

bool WikiTerms::contains(std::string_view language, std::string_view value) const {
    auto set = terms.find(std::string(language));
    if (set == terms.end() || value.size() > maxBytes * 2) // lowercase may change length a little
        return false;
    return set->second.contains(wikilib::unicode::to_lower(value));
}

bool WikiTerms::mayMatch(std::string_view text) const {
    // Labels, aliases and descriptions: {"language":"fr","value":"verbe"}
    static constexpr std::string_view languageKey = R"({"language":")";
    static constexpr std::string_view valueKey = R"(","value":")";
    for (size_t pos = text.find(languageKey); pos != std::string_view::npos;
         pos = text.find(languageKey, pos + 1)) {
        size_t langStart = pos + languageKey.size();
        size_t langEnd = text.find('"', langStart);
        if (langEnd == std::string_view::npos || text.compare(langEnd, valueKey.size(), valueKey) != 0)
            continue;
        auto language = text.substr(langStart, langEnd - langStart);
        if (!terms.contains(std::string(language)))
            continue;
        size_t valueStart = langEnd + valueKey.size();
        size_t valueEnd = stringEnd(text, valueStart);
        if (valueEnd == std::string_view::npos)
            return false;
        auto raw = text.substr(valueStart, valueEnd - valueStart);
        if (raw.size() <= maxBytes * 6 && contains(language, unescapeJson(raw)))
            return true;
        pos = valueEnd;
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> WikiTerms::matches(const json &entity) const {
    std::vector<std::pair<std::string, std::string>> result;
    auto check = [&](const json &item) {
        if (!item.is_object() || !item.contains("language") || !item.contains("value"))
            return;
        const auto &language = item["language"];
        const auto &value = item["value"];
        if (language.is_string() && value.is_string() &&
            contains(language.get<std::string>(), value.get<std::string>()))
            result.emplace_back(language.get<std::string>(), value.get<std::string>());
    };
    if (auto labels = entity.find("labels"); labels != entity.end() && labels->is_object())
        for (const auto &[lang, label] : labels->items())
            check(label);
    if (auto aliases = entity.find("aliases"); aliases != entity.end() && aliases->is_object())
        for (const auto &[lang, list] : aliases->items())
            if (list.is_array())
                for (const auto &alias : list)
                    check(alias);
    return result;
}
