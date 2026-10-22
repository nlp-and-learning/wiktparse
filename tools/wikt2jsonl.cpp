// Converts Wiktionary pages to JSONL (HANDOFF-2, step 4), driven by the
// per-wiki configuration in res/wiktionary: profile defaults (profiles.json,
// "base" inherits), the profile and language template of a wiki (wikis.tsv),
// then res/wiktionary/<wiki>.json.
//
// First version: language sections with their code, headings and template
// lines (parts of speech and sections, named in English later), senses and
// translations. Reads samples written by wiktsamples, not dumps.
//
// wikt2jsonl [-i samples100] [-o jsonl] [--db wiktlangs.sqlite] [wiki...]
//
// Output: <out>/<wiki>.jsonl, one page per line:
//   {"wiki","title","ns","sections":[{"language","code","headings":[...],
//    "native","senses":[{"id","text"}],"translations":[{"lang","term","sense"}]}]}
// ("native": the section is in the wiki's own language)
// <out>/markers.tsv (wiki, form, marker as written, code, sections; read by
// wiktlangs into table wikt_marker) and <out>/stats.tsv (also printed): per wiki pages, sections, sections with
// a language code, senses, translations.
#include <algorithm>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <wikilib/core/unicode_utils.h>
#include <wikilib/templates/template_parser.h>

#include "dump/WikiDump.h"
#include "util/textUtils.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using wikilib::templates::TemplateInvocation;

namespace {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void mergeInto(json &target, const json &patch) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.value().is_object() && target.contains(it.key()) && target[it.key()].is_object())
            mergeInto(target[it.key()], it.value());
        else
            target[it.key()] = it.value();
    }
}

json readJson(const fs::path &file) {
    std::ifstream in(file);
    if (!in)
        throw std::runtime_error("Cannot open " + file.string());
    return json::parse(in);
}

class Configs {
public:
    explicit Configs(const fs::path &dir) : dir_(dir) {
        profiles_ = readJson(dir / "profiles.json").at("profiles");
        for (const auto &line : readLines((dir / "wikis.tsv").string())) {
            auto fields = split(line, '\t');
            if (fields.size() < 2 || fields[0] == "wiki")
                continue;
            wikis_[fields[0]] = {fields[1], fields.size() > 2 ? fields[2] : "", fields.size() > 3 ? fields[3] : ""};
        }
    }

    [[nodiscard]] json of(const std::string &wiki) const {
        auto it = wikis_.find(wiki);
        if (it == wikis_.end())
            throw std::runtime_error("wiki not in wikis.tsv: " + wiki);
        const auto &[profile, languageTemplate, file] = it->second;
        json config = profile_(profile);
        config["profile"] = profile;
        if (!languageTemplate.empty()) {
            if (languageTemplate.starts_with("{{"))
                config["language"]["pattern"] = languageTemplate;
            else
                config["language"]["template"] = languageTemplate;
        }
        if (!file.empty())
            mergeInto(config, readJson(dir_ / file));
        return config;
    }

private:
    struct Wiki {
        std::string profile, languageTemplate, file;
    };
    fs::path dir_;
    json profiles_;
    std::map<std::string, Wiki> wikis_;

    [[nodiscard]] json profile_(const std::string &name) const {
        const auto &p = profiles_.at(name);
        json config = p.contains("base") ? profile_(p["base"].get<std::string>()) : json::object();
        mergeInto(config, p);
        config.erase("base");
        return config;
    }
};

std::string str(const json &config, const std::string &pointer, const std::string &fallback = "") {
    auto ptr = json::json_pointer(pointer);
    return config.contains(ptr) && config[ptr].is_string() ? config[ptr].get<std::string>() : fallback;
}

int num(const json &config, const std::string &pointer, int fallback) {
    auto ptr = json::json_pointer(pointer);
    return config.contains(ptr) && config[ptr].is_number_integer() ? config[ptr].get<int>() : fallback;
}

// ---------------------------------------------------------------------------
// Language names -> en.wikt codes
// ---------------------------------------------------------------------------

std::string withoutCombining(std::string_view text) {
    std::string result;
    for (size_t pos = 0; pos < text.size();) {
        size_t start = pos;
        auto c = wikilib::unicode::decode_utf8(text, pos);
        if (!c) {
            pos = start + 1;
            continue;
        }
        bool combining = (*c >= 0x0300 && *c <= 0x036F) || (*c >= 0x0591 && *c <= 0x05C7) ||
                         (*c >= 0x0610 && *c <= 0x061A) || (*c >= 0x064B && *c <= 0x065F) || *c == 0x0670;
        if (!combining)
            result += text.substr(start, pos - start);
    }
    return result;
}

class LanguageNames {
public:
    explicit LanguageNames(const fs::path &file) {
        if (!fs::exists(file) || sqlite3_open_v2(file.string().c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            std::cerr << "warning: no " << file << ", language names are not resolved" << std::endl;
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }
    ~LanguageNames() { sqlite3_close(db_); }

    // Code of a language name written in the language of the wiki
    // Thread-safe
    // lookup: the full label when the wiki writes a short name (sr "српски" -> "{name} језик")
    // strip: words for "language" that labels may lack (hr "jezik", th "ภาษา")
    std::string code(const std::string &wiki, const std::string &name, const std::string &lookup = "",
                      const std::vector<std::string> &strip = {}) {
        if (!db_ || name.empty())
            return "";
        std::lock_guard lock(mutex_);
        auto key = wiki + '\t' + name + '\t' + lookup;
        if (auto it = cache_.find(key); it != cache_.end())
            return it->second;
        std::string lang = wiki;
        std::ranges::replace(lang, '_', '-');
        // Labels of Chinese wikis are in traditional or simplified characters
        // Wikidata label languages differing from wiki codes
        static const std::map<std::string, std::string> labelLanguage = {
                {"no", "nb"}, {"simple", "en"}, {"zh-min-nan", "nan"}, {"roa-rup", "rup"}, {"bh", "bho"}};
        if (auto it = labelLanguage.find(lang); it != labelLanguage.end())
            lang = it->second;
        std::vector<std::string> langs{lang};
        if (lang == "zh")
            langs = {"zh", "zh-hant", "zh-hans", "zh-tw", "zh-cn", "zh-hk"};
        // "esperanto (morfem)", "język norweski (bokmål)": also without the note
        std::vector<std::string> variants{name};
        if (auto at = lookup.find("{name}"); at != std::string::npos)
            variants.push_back(std::string(lookup).replace(at, 6, name));
        for (const auto &word : strip)
            if (auto at = name.find(word); at != std::string::npos)
                variants.push_back(trim(std::string(name).erase(at, word.size())));
        // SQLite lower() changes ASCII letters only ("Čeština")
        for (size_t i = 0, n = variants.size(); i < n; ++i)
            if (auto lower = wikilib::unicode::to_lower(variants[i]); lower != variants[i])
                variants.push_back(lower);
        // skr "اُردو": without vowel marks and other combining characters
        if (auto plain = withoutCombining(name); plain != name)
            variants.push_back(plain);
        if (auto open = name.rfind(" ("); open != std::string::npos && name.ends_with(")"))
            variants.push_back(name.substr(0, open));
        std::string result;
        // A code written as such ("en" from {{=en=}})
        static const std::regex code(R"(^[a-z]{2,3}(-[a-z]+)*$)");
        if (std::regex_match(name, code))
            result = query("SELECT code FROM lang WHERE code=?2 AND ?1<>'' LIMIT 1", lang, name);
        for (const auto &n : variants)
            for (const auto &l : langs) {
                if (result.empty())
                    result = query("SELECT n.code FROM lang_name n WHERE n.lang=?1 AND n.name=?2 ORDER BY n.alias, (SELECT count(*) FROM lang l WHERE l.code=n.code AND l.kind='language') DESC, length(n.code) LIMIT 1", l, n);
                if (result.empty())
                    result = query("SELECT n.code FROM lang_name n WHERE n.lang=?1 AND lower(n.name)=lower(?2) ORDER BY n.alias, (SELECT count(*) FROM lang l WHERE l.code=n.code AND l.kind='language') DESC, length(n.code) LIMIT 1", l, n);
            }
        if (result.empty())
            result = query("SELECT code FROM lang WHERE name=?2 AND ?1<>'' LIMIT 1", lang, name);
        return cache_[key] = result;
    }

private:
    sqlite3 *db_ = nullptr;
    std::map<std::string, std::string> cache_;
    std::mutex mutex_;

    std::string query(const char *sql, const std::string &a, const std::string &b) {
        sqlite3_stmt *stmt = nullptr;
        std::string result;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, a.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, b.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW)
                result = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
        return result;
    }
};

// ---------------------------------------------------------------------------
// Page structure
// ---------------------------------------------------------------------------

struct Heading {
    int level;
    std::string text;
};

std::optional<Heading> heading(const std::string &line) {
    static const std::regex re(R"(^(=+)\s*(.*?)\s*(=+)\s*$)");
    std::smatch m;
    if (!std::regex_match(line, m, re) || m[2].length() == 0)
        return std::nullopt;
    return Heading{static_cast<int>(std::min(m[1].length(), m[3].length())), m[2].str()};
}

// A line that is one template only: {{-sin-}}, {{znaczenia}}, {{-noun-|nld}}
std::optional<TemplateInvocation> templateLine(const std::string &line) {
    auto t = trim(line);
    if (!t.starts_with("{{") || !t.ends_with("}}"))
        return std::nullopt;
    auto invocations = wikilib::templates::find_invocations(t);
    if (invocations.size() != 1 || invocations[0].location.length() != t.size())
        return std::nullopt;
    return invocations[0];
}

std::optional<TemplateInvocation> onlyTemplate(const std::string &text) {
    return templateLine(text);
}

// Language of a line by the configured form, as written (name, code or template name)
std::optional<std::string> languageMarker(const json &config, const std::string &line) {
    const auto form = str(config, "/language/form");
    const int level = num(config, "/language/heading_level", 2);
    if (form == "line-template") {
        auto t = templateLine(line);
        if (!t)
            return std::nullopt;
        static const std::regex dash(R"(^-([a-z][a-z-]*)-$)"), eq(R"(^=([a-z][a-z-]*)=$)");
        std::smatch m;
        auto pattern = str(config, "/language/pattern", "{{-xx-}}");
        if (pattern == "{{=xx=}}" ? std::regex_match(t->name, m, eq) : std::regex_match(t->name, m, dash))
            return m[1].str();
        return std::nullopt;
    }
    auto h = heading(line);
    if (!h || h->level != level)
        return std::nullopt;
    if (form == "heading-name") {
        // or "[[ଓଡ଼ିଆ]]", ps "[[پښتو]]": the link text
        static const std::regex link(R"(^\[\[(?:[^\]|]*\|)?([^\]|]+)\]\]$)");
        std::smatch m;
        if (std::regex_match(h->text, m, link))
            return m[1].str();
        // my "{{=en=}}", "{{-en-}}": the code
        static const std::regex codeTemplate(R"(^\{\{[=-]([a-z]{2,3}(?:-[a-z]+)*)[=-]\}\}$)");
        if (std::regex_match(h->text, m, codeTemplate))
            return m[1].str();
        return h->text;
    }
    if (form == "heading-dash-template") {
        auto t = onlyTemplate(h->text);
        static const std::regex dash(R"(^-([a-z][a-z-]*)-$)");
        std::smatch m;
        if (t && std::regex_match(t->name, m, dash))
            return m[1].str();
        return std::nullopt;
    }
    if (form == "heading-template") {
        auto t = onlyTemplate(h->text);
        if (!t)
            return std::nullopt;
        auto name = str(config, "/language/template");
        static const std::regex bareCode(R"(^[a-z]{2,3}(-[a-z]{2,8})*$)");
        if (name.empty() && config["language"].value("bare_code", false)) // as: ={{as}}=
            return t->parameters.empty() && std::regex_match(t->name, bareCode) ? std::optional(t->name) : std::nullopt;
        if (!name.empty() && t->name != name) {
            // ja: =={{pt}}== besides =={{L|ja}}==
            static const std::regex code(R"(^[a-z]{2,3}(-[a-z]{2,8})*$)");
            if (config["language"].value("bare_code", false) && t->parameters.empty() && std::regex_match(t->name, code))
                return t->name;
            return std::nullopt;
        }
        int code = num(config, "/language/code", 1);
        if (str(config, "/language/value") == "template" || config["language"].value("code", json(1)).is_null())
            return t->name;
        if (auto v = t->get(static_cast<size_t>(code)))
            return std::string(*v);
        return std::nullopt;
    }
    if (form == "heading-word-and-template") {
        // "woda ({{język polski}})", "Wasser ({{Sprache|Deutsch}})"
        // sr: "sanska koza ([[Викиречник:Српски|српски]], [[…|ћирилица]])": the first link
        if (auto link = h->text.find("([["); link != std::string::npos) {
            auto end = h->text.find("]]", link);
            if (end == std::string::npos)
                return std::nullopt;
            auto target = h->text.substr(link + 3, end - link - 3);
            auto bar = target.find('|');
            return trim(bar == std::string::npos ? target : target.substr(bar + 1));
        }
        auto open = h->text.find("({{");
        if (open == std::string::npos)
            return std::nullopt;
        auto t = onlyTemplate(trim(h->text.substr(open + 1, h->text.rfind(')') - open - 1)));
        if (!t)
            return std::nullopt;
        // pl {{język hiszpański|sort key}}: the template name; de {{Sprache|Deutsch}}: parameter 1
        if (str(config, "/language/name_from") != "template")
            if (auto v = t->get(size_t{1}))
                return std::string(*v);
        return t->name;
    }
    return std::nullopt;
}

struct Sense {
    std::string id, text;
};

// Sense line by the configured marker; ids are explicit or the order of '#' lines
std::optional<Sense> senseLine(const json &config, const std::string &line, std::vector<int> &counters) {
    const auto marker = str(config, "/senses/marker", "#");
    std::smatch m;
    if (marker.starts_with("#")) {
        size_t depth = 0;
        while (depth < line.size() && line[depth] == '#')
            ++depth;
        if (depth == 0 || (depth < line.size() && (line[depth] == ':' || line[depth] == '*')))
            return std::nullopt;
        counters.resize(depth, 0);
        counters[depth - 1]++;
        std::string id;
        for (size_t i = 0; i < depth; ++i)
            id += (i ? "." : "") + std::to_string(counters[i]);
        return Sense{id, trim(line.substr(depth))};
    }
    static const std::regex parenthesized(R"(^:\s*\(([0-9.]+[a-z]?)\)\s*(.*)$)"), bracketed(R"(^:\s*\[([0-9]+[a-z]?)\]\s*(.*)$)"),
            semicolon(R"(^;([0-9]+)\s*(.*)$)");
    const auto &re = marker.starts_with(": (") ? parenthesized : marker.starts_with(":[") ? bracketed : semicolon;
    if (std::regex_match(line, m, re))
        return Sense{m[1].str(), trim(m[2].str())};
    return std::nullopt;
}

struct Translation {
    std::string lang, term, sense;
};

// Templates of text, nested ones after their template
std::vector<TemplateInvocation> allTemplates(std::string_view text) {
    std::vector<TemplateInvocation> result;
    std::function<void(std::string_view)> collect = [&](std::string_view part) {
        for (auto &invocation : wikilib::templates::find_invocations(part)) {
            auto parameters = invocation.parameters;
            result.push_back(std::move(invocation));
            for (const auto &[name, value] : parameters)
                if (value.find("{{") != std::string::npos)
                    collect(value);
        }
    };
    collect(text);
    return result;
}

std::vector<std::string> links(const std::string &text) {
    static const std::regex link(R"(\[\[([^\]|#]+)(?:#[^\]|]*)?(?:\|[^\]]*)?\]\])");
    std::vector<std::string> result;
    for (std::sregex_iterator it(text.begin(), text.end(), link), end; it != end; ++it)
        result.push_back(trim((*it)[1].str()));
    return result;
}

std::vector<Translation> translations(const json &config, const std::string &text) {
    std::vector<Translation> result;
    const auto &tc = config.value("translations", json::object());
    const auto terms = tc.value("terms", json::object());
    std::vector<std::string> names = terms.value("templates", std::vector<std::string>{});
    const int langParam = terms.value("lang", 1);
    const auto termParam = terms.contains("term") ? terms["term"] : json(2);
    const auto senseParam = terms.value("sense", std::string());
    const auto table = tc.value("table", json());
    std::string tableSense; // number of the current table (fr, de)
    for (const auto &t : allTemplates(text)) {
        if (table.is_object() && table.contains("top") &&
            std::ranges::contains(table["top"].get<std::vector<std::string>>(), t.name)) {
            tableSense.clear();
            if (table.contains("number") && table["number"].is_number_integer())
                if (auto v = t.get(static_cast<size_t>(table["number"].get<int>())))
                    tableSense = trim(std::string(*v));
            // ru: {{перев-блок|gloss|en={{t|en|water}}|ab=[[аӡы]]}}
            if (table.value("items", std::string()) == "code-parameters")
                for (const auto &[key, value] : t.parameters)
                    if (!key.empty() && value.find("{{") == std::string::npos)
                        for (const auto &term : links(value))
                            result.push_back({key, term, tableSense});
            continue;
        }
        if (std::ranges::find(names, t.name) == names.end())
            continue;
        auto lang = t.get(static_cast<size_t>(langParam));
        if (!lang)
            continue;
        if (termParam.is_number_integer()) {
            if (auto term = t.get(static_cast<size_t>(termParam.get<int>())); term && !term->empty())
                result.push_back({std::string(*lang), std::string(*term), tableSense});
        } else {
            // es: {{t|en|a1=1|t1=water|t2=...}}
            auto pattern = termParam.get<std::string>();
            for (int i = 1; i < 50; ++i) {
                auto key = pattern;
                key.replace(key.find("{i}"), 3, std::to_string(i));
                auto term = t.get(key);
                if (!term)
                    break;
                std::string sense = tableSense;
                if (!senseParam.empty()) {
                    auto skey = senseParam;
                    skey.replace(skey.find("{i}"), 3, std::to_string(i));
                    if (auto s = t.get(skey))
                        sense = std::string(*s);
                }
                result.push_back({std::string(*lang), std::string(*term), sense});
            }
        }
    }
    // Lines of links: pl "* angielski: (1.1) [[water]]", it ":*{{en}}: [[water]]", zh "*英语：[[water]]"
    if (terms.contains("line")) {
        static const std::regex line(R"(^:?\*:?\s*(\{\{([^}|]+)\}\}|[^:：]+?)\s*[:：]\s*(?:\(([0-9.,\s-]+)\)\s*)?(.*)$)");
        std::istringstream in(text);
        for (std::string l; std::getline(in, l);) {
            std::smatch m;
            if (!std::regex_match(l, m, line))
                continue;
            std::string lang = m[2].matched ? m[2].str() : trim(m[1].str());
            for (const auto &term : links(m[4].str()))
                result.push_back({lang, term, m[3].str()});
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

struct Stats {
    size_t pages = 0, pagesWithSections = 0, sections = 0, sectionsWithCode = 0, senses = 0, translations = 0;
    size_t nativeSections = 0, nativeSenses = 0, nativeTranslations = 0;
    // Language markers as written in the wiki -> code and number of sections
    std::map<std::string, std::pair<std::string, size_t>> markers;
    std::map<std::string, std::string> markerExamples; // a page title per marker

    void add(const Stats &o) {
        pages += o.pages, pagesWithSections += o.pagesWithSections, sections += o.sections;
        sectionsWithCode += o.sectionsWithCode, senses += o.senses, translations += o.translations;
        nativeSections += o.nativeSections, nativeSenses += o.nativeSenses, nativeTranslations += o.nativeTranslations;
        for (const auto &[marker, value] : o.markers) {
            markers[marker].first = value.first;
            markers[marker].second += value.second;
        }
        for (const auto &[marker, title] : o.markerExamples)
            markerExamples.emplace(marker, title);
    }
};

// Codes of the wiki's own language: entries of other languages are defined in
// it and have no translation tables (pl: ": (1.1) [[pies]]" under a foreign word)
std::vector<std::string> nativeCodes(const std::string &wiki, const json &config) {
    if (config.contains(json::json_pointer("/language/native")))
        return config["language"]["native"].get<std::vector<std::string>>();
    static const std::map<std::string, std::vector<std::string>> special = {
            {"simple", {"en"}}, {"zh_min_nan", {"nan", "nan-hbl"}}, {"no", {"no", "nb"}}, {"roa_rup", {"rup"}}};
    if (auto it = special.find(wiki); it != special.end())
        return it->second;
    std::string code = wiki;
    std::ranges::replace(code, '_', '-');
    return {code};
}

json convertPage(const std::string &wiki, const json &config, LanguageNames &names, const std::string &title, int ns,
                 const std::string &text, Stats &stats, bool markersOnly = false) {
    json sections = json::array();
    json *current = nullptr;
    std::string sectionText;
    std::vector<int> counters;
    const bool byName = str(config, "/language/value") == "name";
    const auto native = nativeCodes(wiki, config);
    auto finish = [&] {
        if (!current || markersOnly)
            return;
        for (const auto &t : translations(config, sectionText))
            (*current)["translations"].push_back({{"lang", t.lang}, {"term", t.term}, {"sense", t.sense}});
        stats.translations += (*current)["translations"].size();
        stats.senses += (*current)["senses"].size();
        if ((*current)["native"].get<bool>()) {
            stats.nativeSections++;
            stats.nativeSenses += (*current)["senses"].size();
            stats.nativeTranslations += (*current)["translations"].size();
        }
        sectionText.clear();
        counters.clear();
    };
    const bool implicit = str(config, "/language/form") == "implicit";
    const bool requireCode = byName && !implicit && config["language"].value("require_code", true);
    std::istringstream in(text);
    bool first = true;
    for (std::string line; std::getline(in, line);) {
        auto marker = implicit ? (first ? std::optional(native.front()) : std::nullopt) : languageMarker(config, line);
        first = false;
        if (marker && requireCode && !(config.contains(json::json_pointer("/language/aliases")) &&
                                       config["language"]["aliases"].contains(*marker)) &&
            names.code(wiki, *marker, str(config, "/language/lookup"), config["language"].value("strip", std::vector<std::string>{})).empty()) {
            // A heading that is no language (sections at the same level: "Napomene",
            // "Референце"); counted for review, the section goes on
            auto &[markerCode, count] = stats.markers[*marker];
            ++count;
            stats.markerExamples.emplace(*marker, title);
            marker.reset();
        }
        if (marker) {
            finish();
            std::string code = byName && !implicit ? names.code(wiki, *marker, str(config, "/language/lookup"), config["language"].value("strip", std::vector<std::string>{})) : *marker;
            if (implicit && sections.empty() && line.empty() == false)
                sectionText += line + '\n';
            // Names missing in wiktlangs.sqlite: "language": {"aliases": {"język chorwacki": "hr"}}
            if (auto aliases = json::json_pointer("/language/aliases"); config.contains(aliases))
                if (auto it = config[aliases].find(*marker); it != config[aliases].end())
                    code = it->get<std::string>();
            auto &[markerCode, count] = stats.markers[*marker];
            markerCode = code;
            ++count;
            stats.markerExamples.emplace(*marker, title);
            sections.push_back({{"language", *marker}, {"code", code.empty() ? json() : json(code)},
                                {"native", std::ranges::contains(native, code)},
                                {"headings", json::array()}, {"senses", json::array()}, {"translations", json::array()}});
            current = &sections.back();
            stats.sections++;
            stats.sectionsWithCode += !code.empty();
            continue;
        }
        if (!current || markersOnly)
            continue;
        sectionText += line + '\n';
        if (auto h = heading(line))
            (*current)["headings"].push_back(h->text);
        else if (auto t = templateLine(line); t && t->name.size() < 40)
            (*current)["headings"].push_back("{{" + t->name + "}}");
        else if (auto s = senseLine(config, line, counters))
            (*current)["senses"].push_back({{"id", s->id}, {"text", s->text}});
    }
    finish();
    stats.pages++;
    stats.pagesWithSections += !sections.empty();
    return {{"wiki", wiki}, {"title", title}, {"ns", ns}, {"sections", sections}};
}

} // namespace

int main(int argc, char *argv[]) {
    try {
        fs::path input = "samples100", output = "jsonl", dbFile = "wiktlangs.sqlite";
        fs::path configDir = WIKTPARSE_RES_DIR "/wiktionary";
        std::vector<std::string> wikis;
        bool fromDumps = false, markersOnly = false;
        size_t threads = 8;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-i" && i + 1 < argc)
                input = argv[++i];
            else if (arg == "-o" && i + 1 < argc)
                output = argv[++i];
            else if (arg == "--db" && i + 1 < argc)
                dbFile = argv[++i];
            else if (arg == "--dump")
                fromDumps = true;
            else if (arg == "--markers-only")
                markersOnly = true;
            else if (arg == "-j" && i + 1 < argc)
                threads = std::max(1, std::stoi(argv[++i]));
            else if (arg == "-h" || arg == "--help") {
                std::cout << "usage: wikt2jsonl [-i samples100 | --dump [-j N]] [--markers-only] [-o jsonl] [--db wiktlangs.sqlite] [wiki...]\n"
                             "       --dump: all pages of the dumps, each wiki read in N threads (default 8)\n"
                             "       --markers-only: only language markers and stats, no JSONL\n";
                return 0;
            } else
                wikis.push_back(arg);
        }
        if (wikis.empty() && fromDumps)
            wikis = WikiDump::dumpPath(wikilib::dump::WikiProject::Wiktionary, "en").available_languages();
        else if (wikis.empty())
            for (const auto &entry : fs::directory_iterator(input))
                if (entry.is_directory())
                    wikis.push_back(entry.path().filename().string());
        std::ranges::sort(wikis);

        Configs configs(configDir);
        LanguageNames names(dbFile);
        fs::create_directories(output);
        std::ofstream statsFile(output / "stats.tsv");
        // For wiktlangs.sqlite (table wikt_marker): how each wiki writes languages
        std::ofstream markersFile(output / "markers.tsv");
        markersFile << "wiki\tform\tmarker\tcode\tsections\texample\n";
        std::string header = "wiki\tprofile\tpages\twith_sections\tsections\twith_code\tsenses\ttranslations\tnative_sections\tnative_senses\tnative_translations";
        statsFile << header << '\n';
        std::cout << header << '\n';
        for (const auto &wiki : wikis) {
            json config = configs.of(wiki);
            if (config.value("skip", false))
                continue;
            Stats stats;
            std::ofstream out;
            if (!markersOnly)
                out.open(output / (wiki + ".jsonl"));
            if (fromDumps) {
                auto dump = WikiDump::wiktionary(wiki);
                std::vector<Stats> workerStats(dump.workerCount(threads));
                std::mutex outMutex;
                std::cout << wiki << ": " << dump.path().dump_path().filename().string() << std::endl;
                dump.forEachPageParallel(
                        [&](size_t worker, const std::string &title, const std::string &text) {
                            auto line = convertPage(wiki, config, names, title, 0, text, workerStats[worker], markersOnly);
                            if (!markersOnly) {
                                auto dumped = line.dump(-1, ' ', false, json::error_handler_t::replace);
                                std::lock_guard lock(outMutex);
                                out << dumped << '\n';
                            }
                        },
                        threads, 0.25);
                for (const auto &w : workerStats)
                    stats.add(w);
            } else
            for (const auto &page : readJson(input / wiki / "meta.json")) {
                std::ifstream file(input / wiki / page["file"].get<std::string>());
                std::stringstream text;
                text << file.rdbuf();
                auto line = convertPage(wiki, config, names, page["title"], page["ns"], text.str(), stats, markersOnly);
                if (!markersOnly)
                    out << line.dump(-1, ' ', false, json::error_handler_t::replace) << '\n';
            }
            auto row = std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", wiki, config.value("profile", ""), stats.pages,
                                   stats.pagesWithSections, stats.sections, stats.sectionsWithCode, stats.senses,
                                   stats.translations, stats.nativeSections, stats.nativeSenses, stats.nativeTranslations);
            statsFile << row << '\n';
            for (const auto &[marker, value] : stats.markers)
                if (!value.first.empty() || value.second >= 3) // one-off unresolved headings are words or sections
                markersFile << wiki << '\t' << str(config, "/language/form") << '\t' << marker << '\t' << value.first
                            << '\t' << value.second << '\t' << stats.markerExamples[marker] << '\n';
            std::cout << row << std::endl;
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
