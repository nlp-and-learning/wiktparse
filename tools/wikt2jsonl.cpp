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
#include <set>
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

#include <wikilib/lua/lua_data.h>

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
        // Headings lose zero-width characters (si "ඉංග්‍රීසි" has a joiner): aliases too
        if (auto ptr = json::json_pointer("/language/aliases"); config.contains(ptr)) {
            json &aliases = config[ptr];
            for (const auto &[key, value] : json(aliases).items()) {
                std::string plain = key;
                for (std::string_view zw : {"\u200B", "\u200C", "\u200D", "\u200E", "\u200F"})
                    for (size_t at; (at = plain.find(zw)) != std::string::npos;)
                        plain.erase(at, zw.size());
                if (plain != key && !aliases.contains(plain))
                    aliases[plain] = value;
            }
        }
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
        // Names from Lua modules of the wiki itself (wiktmodules -> wikt_lang_name)
        for (const auto &n : variants)
            if (result.empty())
                result = query("SELECT code FROM wikt_lang_name WHERE wiki=?1 AND name=?2 LIMIT 1", wiki, n);
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
        // bcl "<small>Etimolohiya</small>", "'''Kahulugan'''", am " እንግሊዝኛ " (no-break spaces)
        static const std::regex wrappers(R"(<[^>]*>|''+)");
        h->text = std::regex_replace(h->text, wrappers, "");
        for (std::string_view space : {"\u00A0", "\u2009", "\u200B"})
            for (size_t at; (at = h->text.find(space)) != std::string::npos;)
                h->text.replace(at, space.size(), " ");
        // fa "[[تصویر:Flag of Italy.svg|40px]] ایتالیایی": without the image; no zero-width joiners
        static const std::regex image(R"(^\[?\[?(?:تصویر|[Ff]ile|[Ii]mage|پرونده):[^\]]*\]\]\s*(?:\[\[)?)");
        h->text = std::regex_replace(h->text, image, "");
        for (std::string_view zw : {"\u200C", "\u200D", "\u200E", "\u200F"})
            for (size_t at; (at = h->text.find(zw)) != std::string::npos;)
                h->text.erase(at, zw.size());
        h->text = trim(h->text);
        // gn "Techapyrã:", gom "Konknni /"
        while (!h->text.empty() && std::string_view(":./").find(h->text.back()) != std::string_view::npos)
            h->text = trim(h->text.substr(0, h->text.size() - 1));
        // or "[[ଓଡ଼ିଆ]]", ps "[[پښتو]]": the link text
        static const std::regex link(R"(^\[\[(?:[^\]|]*\|)?([^\]|]+)\]\]$)");
        std::smatch m;
        if (std::regex_match(h->text, m, link))
            return trim(m[1].str());
        // ang "{{sprǣc|en}}": a code parameter; "{{fro}}": a code as the name; fa "{{انگلیسی}}": a name;
        // fa "{{{3|فارسی}}}": the default of a parameter
        static const std::regex parameterDefault(R"(^\{\{\{[^|}]*\|([^}]+)\}\}\}$)");
        if (std::regex_match(h->text, m, parameterDefault))
            return trim(m[1].str());
        if (auto t = onlyTemplate(h->text); t && !t->name.starts_with("=") && !t->name.starts_with("-")) {
            static const std::regex code(R"(^[a-z]{2,3}(-[a-z]+)*$)");
            if (auto v = t->get(size_t{1}); v && std::regex_match(std::string(*v), code))
                return std::string(*v);
            if (t->parameters.empty())
                return t->name;
        }
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

// Known section headings per wiki (res/wiktionary_section_names.tsv): not listed as unresolved languages
std::map<std::string, std::set<std::string>> knownSections;

// English parts of speech and relations; other sections keep kind "section"
const std::set<std::string> partsOfSpeech = {
        "noun", "verb", "adjective", "adverb", "pronoun", "proper noun", "numeral", "interjection", "conjunction",
        "preposition", "postposition", "particle", "determiner", "article", "suffix", "prefix", "infix", "interfix",
        "circumfix", "affix", "phrase", "prepositional phrase", "idiom", "proverb", "letter", "symbol", "participle",
        "contraction", "abbreviation", "initialism", "acronym", "classifier", "counter", "root", "han character",
        "kanji", "hanja", "syllable", "punctuation mark", "diacritical mark", "number", "adnominal", "verbal noun",
        "inflected form"};
const std::set<std::string> relations = {
        "synonyms", "antonyms", "hypernyms", "hyponyms", "meronyms", "holonyms", "troponyms", "coordinate terms",
        "derived terms", "related terms", "compounds", "descendants", "anagrams", "idioms", "proverbs",
        "collocations", "paronyms", "abbreviations", "diminutives", "false friends", "homonyms", "metagrams"};

struct SectionName {
    std::string section; // English: "Synonyms"
    std::string kind;    // pos, relation, section
};

// Names of sections of a wiki (wiktsections.sqlite): its own names (by hand), Wikidata labels in
// its language, English names; keys are lowercase
class SectionNames {
public:
    SectionNames(const fs::path &file, const std::string &wiki) {
        sqlite3 *db = nullptr;
        if (!fs::exists(file) || sqlite3_open_v2(file.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            sqlite3_close(db);
            return;
        }
        std::string lang = wiki == "simple" ? "en" : wiki;
        std::ranges::replace(lang, '_', '-');
        auto load = [&](const char *sql) {
            sqlite3_stmt *stmt = nullptr;
            sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
            sqlite3_bind_text(stmt, 1, wiki.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, lang.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                std::string section = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                std::string name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                names_.emplace(wikilib::unicode::to_lower(name), section);
            }
            sqlite3_finalize(stmt);
        };
        load("SELECT section, name FROM section_name WHERE lang=?1 AND source='manual'");
        load("SELECT section, name FROM section_name WHERE lang=?2 AND source='wikidata-label'");
        load("SELECT section, name FROM section_name WHERE lang=?2 AND source='wikidata-alias'");
        load("SELECT section, section FROM section WHERE ?1<>'' AND ?2<>''");
        sqlite3_close(db);
    }

    [[nodiscard]] std::optional<SectionName> find(const std::string &key) const {
        auto it = names_.find(wikilib::unicode::to_lower(trim(key)));
        if (it == names_.end())
            return std::nullopt;
        // "Synonyms", "references" (by hand): English capitalized
        std::string section = it->second;
        auto lower = wikilib::unicode::to_lower(section);
        // First letter only ("Derived terms")
        if (size_t pos = 0; !section.empty() && wikilib::unicode::decode_utf8(section, pos))
            section = wikilib::unicode::to_upper(section.substr(0, pos)) + section.substr(pos);
        std::string kind = partsOfSpeech.contains(lower) ? "pos" : relations.contains(lower) ? "relation" : "section";
        return SectionName{section, kind};
    }

private:
    std::map<std::string, std::string> names_; // first source wins
};

// Key of a heading or template line for SectionNames: the text, a template's
// name or its first parameter (fr {{S|nom|fr}}, de {{Wortart|Substantiv|Deutsch}}),
// without {{-…-}} dashes (it {{-sin-}}, nl {{-noun-|nld}}) and numbering ("Etymology 2")
std::pair<std::string, int> sectionKey(std::string text) {
    static const std::regex numbered(R"(^(.*\S)\s+(\d+)$)");
    text = trim(text);
    if (text.starts_with("{{"))
        if (auto all = wikilib::templates::find_invocations(text); !all.empty())
            text = text.substr(0, all.front().location.length()); // de "{{Wortart|Substantiv|Deutsch}}, {{n}}"
    if (auto t = onlyTemplate(trim(text))) {
        static const std::set<std::string> byParameter = {"S", "Wortart", "-pos-"};
        if (byParameter.contains(t->name)) {
            if (auto v = t->get(size_t{1}))
                text = std::string(*v);
        } else {
            text = t->name;
            while (!text.empty() && (text.front() == '-' || text.front() == '='))
                text.erase(0, 1);
            while (!text.empty() && (text.back() == '-' || text.back() == '='))
                text.pop_back();
        }
    } else if (auto open = text.find("{{"); open != std::string::npos && open > 0) {
        text = text.substr(0, open); // de "{{Wortart|…}}, {{n}}" is handled above; "Noun {{…}}"
    }
    text = trim(text);
    std::smatch m;
    if (std::regex_match(text, m, numbered))
        return {m[1].str(), std::stoi(m[2].str())};
    return {text, 0};
}

std::optional<Heading> wikt2jsonl_heading(const std::string &line) { return heading(line); }

// Entries of one language section: etymologies, in them parts of speech with
// senses, sections (raw wikitext under English names) and translations
class LanguageEntry {
public:
    LanguageEntry(const json &config, const SectionNames &names) : config_(config), names_(names) {
        entry_ = {{"etymologies", json::array({newEtymology(0)})}, {"sections", json::object()},
                  {"unknown_headings", json::array()}};
    }

    void line(const std::string &line) {
        std::optional<std::string> raw;
        if (auto h = wikt2jsonl_heading(line))
            raw = h->text;
        else if (auto t = str(config_, "/sections/form") == "heading" ? std::nullopt : templateLine(line);
                 t && t->name.size() < 40 && names_.find(sectionKey(trim(line)).first))
            raw = trim(line); // {{synonimy}}, {{-sin-}}; not {{trans-top|…}}
        if (raw) {
            onHeading(*raw);
            return;
        }
        // pl: part of speech as an italic line after {{znaczenia}}: ''rzeczownik, rodzaj żeński''
        if (str(config_, "/pos/form") == "italic-line" && line.starts_with("''") && section_ == "Definitions") {
            auto text = line.substr(2, line.find("''", 2) == std::string::npos ? std::string::npos : line.find("''", 2) - 2);
            auto first = trim(text.substr(0, text.find(',')));
            if (auto name = names_.find(first); name && name->kind == "pos") {
                startPos(name->section, text);
                return;
            }
        }
        // Senses only under the part of speech or in its definitions (pl ": (1.1)" also marks synonyms)
        if (auto s = section_.empty() || section_ == "Definitions" ? senseLine(config_, line, counters_) : std::nullopt) {
            pos()["senses"].push_back({{"id", s->id}, {"text", s->text}});
            return;
        }
        if (!section_.empty()) {
            auto &sections = container()["sections"];
            sections[section_] = sections.value(section_, std::string()) + line + '\n';
        }
    }

    // Translations of the parts of speech from their translation sections
    json finish(Stats &stats) {
        for (auto &ety : entry_["etymologies"])
            for (auto &p : ety["pos"]) {
                // The translation section, or all sections when its local name is not known yet
                std::string text;
                if (auto it = p["sections"].find("Translations"); it != p["sections"].end())
                    text = it->get<std::string>();
                else
                    for (const auto &[name, section] : p["sections"].items())
                        text += section.get<std::string>();
                for (const auto &t : translations(config_, text))
                    p["translations"].push_back({{"lang", t.lang}, {"term", t.term}, {"sense", t.sense}});
                stats.senses += p["senses"].size();
                stats.translations += p["translations"].size();
            }
        // The first etymology when empty (pages without etymology sections)
        auto &etys = entry_["etymologies"];
        if (etys.size() > 1 && etys[0]["pos"].empty() && etys[0]["sections"].empty())
            etys.erase(0);
        return entry_;
    }

private:
    const json &config_;
    const SectionNames &names_;
    json entry_;
    std::string section_;
    bool inPos_ = false;
    std::vector<int> counters_;

    static json newEtymology(int index) {
        return {{"index", index ? json(index) : json()}, {"sections", json::object()}, {"pos", json::array()}};
    }
    json &etymology() { return entry_["etymologies"].back(); }
    json &pos() {
        if (etymology()["pos"].empty())
            startPos("", "");
        return etymology()["pos"].back();
    }
    json &container() { return inPos_ ? pos() : etymology(); }

    void startPos(const std::string &pos, const std::string &heading) {
        etymology()["pos"].push_back({{"pos", pos.empty() ? json() : json(pos)}, {"heading", heading},
                                      {"senses", json::array()}, {"sections", json::object()},
                                      {"translations", json::array()}});
        inPos_ = true;
        counters_.clear();
    }

    void onHeading(const std::string &raw) {
        auto [key, index] = sectionKey(raw);
        auto name = names_.find(key);
        if (!name) {
            entry_["unknown_headings"].push_back(key);
            section_ = key;
            return;
        }
        // Wikis without homographs under etymologies (pl, it): etymology is a section like others
        if (name->section == "Etymology" && config_.contains("homographs") && config_["homographs"].is_null()) {
            section_ = "Etymology";
            return;
        }
        if (name->section == "Etymology") {
            // "Etymology 2", or an etymology after parts of speech: a new one
            if (index > 0 && (etymology()["index"].is_number() || !etymology()["pos"].empty()))
                entry_["etymologies"].push_back(newEtymology(index));
            else if (index > 0)
                etymology()["index"] = index;
            else if (!etymology()["pos"].empty())
                entry_["etymologies"].push_back(newEtymology(0));
            inPos_ = false;
            section_ = "Etymology";
            return;
        }
        if (name->kind == "pos") {
            startPos(name->section, raw);
            section_.clear();
            return;
        }
        section_ = name->section;
    }
};

json convertPage(const std::string &wiki, const json &config, LanguageNames &names, const SectionNames &sectionNames,
                 const std::string &title, int ns, const std::string &text, Stats &stats, bool markersOnly = false) {
    json sections = json::array();
    json *current = nullptr;
    std::optional<LanguageEntry> entry;
    const bool byName = str(config, "/language/value") == "name";
    const auto native = nativeCodes(wiki, config);
    auto finish = [&] {
        if (!current || markersOnly || !entry)
            return;
        const size_t senses = stats.senses, translated = stats.translations;
        current->update(entry->finish(stats));
        if ((*current)["native"].get<bool>()) {
            stats.nativeSections++;
            stats.nativeSenses += stats.senses - senses;
            stats.nativeTranslations += stats.translations - translated;
        }
        entry.reset();
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
            if (!knownSections[wiki].contains(*marker)) {
                auto &[markerCode, count] = stats.markers[*marker];
                ++count;
                stats.markerExamples.emplace(*marker, title);
            }
            marker.reset();
        }
        if (marker) {
            finish();
            std::string code = byName && !implicit ? names.code(wiki, *marker, str(config, "/language/lookup"), config["language"].value("strip", std::vector<std::string>{})) : *marker;
            const bool keepLine = implicit && sections.empty() && !line.empty();
            // Names missing in wiktlangs.sqlite: "language": {"aliases": {"język chorwacki": "hr"}}
            if (auto aliases = json::json_pointer("/language/aliases"); config.contains(aliases))
                if (auto it = config[aliases].find(*marker); it != config[aliases].end())
                    code = it->get<std::string>();
            auto &[markerCode, count] = stats.markers[*marker];
            markerCode = code;
            ++count;
            stats.markerExamples.emplace(*marker, title);
            sections.push_back({{"language", *marker}, {"code", code.empty() ? json() : json(code)},
                                {"native", std::ranges::contains(native, code)}});
            current = &sections.back();
            entry.emplace(config, sectionNames);
            if (keepLine && !markersOnly)
                entry->line(line);
            stats.sections++;
            stats.sectionsWithCode += !code.empty();
            continue;
        }
        if (!current || markersOnly)
            continue;
        entry->line(line);
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
        for (const auto &line : readLines((configDir.parent_path() / "wiktionary_section_names.tsv").string())) {
            auto f = split(line, '\t');
            if (f.size() >= 2 && f[0] != "wiki" && !f[0].starts_with("#"))
                knownSections[f[0]].insert(f[1]);
        }
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
            // zh: a Lua module of the wiki maps language names to codes
            // ("Module:Languages/canonical names": ["阿歐語"] = "aou"); its entries become aliases
            if (auto module = str(config, "/language/names_module"); !module.empty()) {
                auto pages = WikiDump::wiktionary(wiki).extractMatching([&](const std::string &t) { return t == module; });
                if (auto it = pages.find(module); it != pages.end()) {
                    auto open = it->second.find('{');
                    auto table = open == std::string::npos ? json() : wikilib::lua::parse_value(std::string_view(it->second).substr(open));
                    size_t added = 0;
                    if (table.is_object())
                        for (const auto &[name, code] : table.items())
                            if (code.is_string() && !config["language"]["aliases"].contains(name)) {
                                config["language"]["aliases"][name] = code;
                                ++added;
                            }
                    std::cout << wiki << ": " << added << " language names from " << module << std::endl;
                }
            }
            if (config.value("skip", false))
                continue;
            const SectionNames sectionNames("wiktsections.sqlite", wiki);
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
                            auto line = convertPage(wiki, config, names, sectionNames, title, 0, text, workerStats[worker], markersOnly);
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
                auto line = convertPage(wiki, config, names, sectionNames, page["title"], page["ns"], text.str(), stats, markersOnly);
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
