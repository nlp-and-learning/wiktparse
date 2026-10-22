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
// Output: <out>/<wiki>.jsonl, one page per line (pages without a recognized
// language are left out; stats.tsv counts them: pages - with_sections):
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

#include <bzlib.h>
#include <wikilib/core/progress.h>
#include <wikilib/dump/dump_reader.h>
#include <wikilib/dump/parallel_reader.h>
#include <wikilib/lua/lua_data.h>
#include <wikilib/markup/tokenizer.h>

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
        // Other forms of language markers, prepared once: {"language": merged without aliases}
        if (config["language"].contains("also")) {
            json base = config["language"];
            for (const char *key : {"also", "aliases", "template", "code", "pattern", "bare_code", "name_from"})
                base.erase(key);
            config["also_forms"] = json::array();
            for (const auto &other : config["language"]["also"]) {
                json merged = base;
                mergeInto(merged, other);
                config["also_forms"].push_back({{"language", merged}});
            }
        }
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

    // en.wikt code of a code of another scheme (wd_code_map: ISO 639-1/3, Wikimedia)
    std::string mappedCode(const std::string &code) {
        if (!db_ || code.empty() || code.size() > 12)
            return "";
        std::lock_guard lock(mutex_);
        if (auto it = mapped_.find(code); it != mapped_.end())
            return it->second;
        return mapped_[code] = query("SELECT wikt_code FROM wd_code_map WHERE code=?2 AND wikt_code<>'' AND ?1<>'' LIMIT 1", "x", code);
    }

    // A language code of wiktlangs.sqlite (en.wikt codes and extra ones)
    bool isCode(const std::string &code) {
        if (!db_ || code.empty())
            return false;
        std::lock_guard lock(mutex_);
        if (auto it = codes_.find(code); it != codes_.end())
            return it->second;
        return codes_[code] = !query("SELECT code FROM lang WHERE code=?2 AND ?1<>'' LIMIT 1", "x", code).empty();
    }

private:
    sqlite3 *db_ = nullptr;
    std::map<std::string, std::string> cache_;
    std::map<std::string, bool> codes_;
    std::map<std::string, std::string> mapped_;
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
std::optional<std::string> languageMarkerOfForm(const json &config, const std::string &line);

// Language of a line: the template ways (language.parameter: nds {{…|SPRAAK=ga}};
// language.template_regex: kk {{srces}}, hu {{engfn}}), the configured form, then
// the other forms of language.also (la {{caput|la}} and =={{-en-}}==)
std::optional<std::string> languageMarker(const json &config, const std::string &line) {
    const auto &language = config["language"];
    if (language.contains("parameter") || language.contains("template_regex"))
        if (auto t = templateLine(line)) {
            if (language.contains("parameter"))
                if (auto v = t->get(language["parameter"].get<std::string>()); v && !v->empty())
                    return std::string(*v);
            if (language.contains("template_regex")) {
                std::smatch m;
                // Compiled once per pattern and thread
                thread_local std::map<std::string, std::regex> compiled;
                const auto &pattern = language["template_regex"].get_ref<const std::string &>();
                auto it = compiled.find(pattern);
                if (it == compiled.end())
                    it = compiled.emplace(pattern, std::regex(pattern)).first;
                if (std::regex_match(t->name, m, it->second) && m.size() > 1)
                    return m[1].str();
            }
        }
    if (auto marker = languageMarkerOfForm(config, line))
        return marker;
    if (config.contains("also_forms"))
        for (const auto &alternative : config["also_forms"])
            if (auto marker = languageMarkerOfForm(alternative, line))
                return marker;
    return std::nullopt;
}

std::optional<std::string> languageMarkerOfForm(const json &config, const std::string &line) {
    const auto form = str(config, "/language/form");
    const int level = num(config, "/language/heading_level", 2);
    if (form == "line-template") {
        auto t = templateLine(line);
        if (!t)
            return std::nullopt;
        // la {{caput|la|rufus}}: a named template with the code as a parameter
        if (auto name = str(config, "/language/template"); !name.empty()) {
            if (t->name != name)
                return std::nullopt;
            if (auto v = t->get(static_cast<size_t>(num(config, "/language/code", 1))); v && !v->empty())
                return std::string(*v);
            return std::nullopt;
        }
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

std::vector<Translation> translations(const json &config, const std::string &text, bool lines = true) {
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
    if (lines && terms.contains("line")) {
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

// The sections of one language section as a tree, as the page has them: a
// heading opens a node below the nearest heading of a lower level; a template
// line (pl {{synonimy}}, it {{-sin-}}) opens a node one level below the last
// heading, its siblings following. Every node: English name ("section", null
// when not known), "kind" (pos, etymology, relation, section), "heading" as
// written, "level", "index" (Etymology 2), raw "text", "senses" where the
// definitions stand, "translations" in translation sections (not linked to
// senses yet), "children".
class LanguageEntry {
public:
    using LanguageCode = std::function<std::string(const std::string &)>;

    LanguageEntry(const json &config, const SectionNames &names, LanguageCode languageCode, int languageLevel)
        : config_(config), names_(names), languageCode_(std::move(languageCode)) {
        root_ = node(std::nullopt, "", "", languageLevel, 0);
        stack_.push_back(&root_);
    }

    // offset: of the line in the page text (bytes); nodes get "range": [begin, end)
    void line(const std::string &line, size_t offset) {
        offset_ = offset;
        if (auto h = wikt2jsonl_heading(line)) {
            open(h->text, h->level, false);
            return;
        }
        if (str(config_, "/sections/form") != "heading")
            if (auto t = templateLine(line); t && t->name.size() < 40 && names_.find(sectionKey(trim(line)).first)) {
                open(trim(line), headingLevel() + 1, true); // {{synonimy}}, {{-sin-}}; not {{trans-top|…}}
                return;
            }
        // pl: part of speech as an italic line in {{znaczenia}}: ''rzeczownik, rodzaj żeński''
        if (str(config_, "/pos/form") == "italic-line" && line.starts_with("''") &&
            (current()["section"] == "Definitions" || current().contains("template_pos"))) {
            auto text = line.substr(2, line.find("''", 2) == std::string::npos ? std::string::npos : line.find("''", 2) - 2);
            auto first = trim(text.substr(0, text.find(',')));
            if (auto name = names_.find(first); name && name->kind == "pos") {
                if (current().contains("template_pos"))
                    close(); // the previous part of speech of these definitions
                json pos = node(name->section, "pos", text, current()["level"].get<int>() + 1, 0);
                pos["template_pos"] = true;
                push(std::move(pos));
                counters_.clear();
                return;
            }
        }
        // Senses where definitions stand: under a part of speech, in definitions ("#", pl ": (1.1)")
        const auto &kind = current()["kind"];
        if (kind == "pos" || current()["section"] == "Definitions")
            if (auto s = senseLine(config_, line, counters_)) {
                current()["senses"].push_back({{"id", s->id}, {"text", s->text}});
                return;
            }
        current()["text"] = current()["text"].get<std::string>() + line + '\n';
    }

    json finish(Stats &stats) {
        finishNode(root_, stats);
        json result = {{"sections", root_.value("children", json::array())}};
        if (root_.contains("text") && !root_["text"].get<std::string>().empty())
            result["text"] = root_["text"];
        if (!unknown_.empty())
            result["unknown_headings"] = unknown_;
        return result;
    }

private:
    const json &config_;
    const SectionNames &names_;
    LanguageCode languageCode_;
    json root_;
    std::vector<json *> stack_; // open nodes, root first
    json unknown_ = json::array();
    std::vector<int> counters_;
    size_t offset_ = 0;

    json &current() { return *stack_.back(); }
    void close() { stack_.pop_back(); }

    int headingLevel() const {
        for (auto it = stack_.rbegin(); it != stack_.rend(); ++it)
            if (!(*it)->value("template", false) && !(*it)->contains("template_pos"))
                return (**it)["level"].get<int>();
        return 1;
    }

    static json node(const std::optional<std::string> &section, const std::string &kind, const std::string &heading,
                     int level, int index) {
        return {{"section", section ? json(*section) : json()}, {"kind", kind.empty() ? json() : json(kind)},
                {"heading", heading}, {"level", level}, {"index", index ? json(index) : json()}, {"text", ""},
                {"senses", json::array()}, {"translations", json::array()}, {"children", json::array()}};
    }

    void push(json n) {
        auto &children = current()["children"];
        children.push_back(std::move(n));
        stack_.push_back(&children.back());
    }

    void open(const std::string &raw, int level, bool templateLine) {
        // Close nodes at this level or deeper (template lines: also earlier template lines)
        while (stack_.size() > 1 && current()["level"].get<int>() >= level)
            close();
        auto [key, index] = sectionKey(raw);
        auto name = names_.find(key);
        if (!name)
            unknown_.push_back(key);
        std::string kind = !name ? "" : name->section == "Etymology" ? "etymology" : name->kind;
        json n = node(name ? std::optional(name->section) : std::nullopt, kind, raw, level, index);
        if (templateLine)
            n["template"] = true;
        push(std::move(n));
        counters_.clear();
    }

    void finishNode(json &n, Stats &stats) {
        // Translations of translation sections; of a part of speech without one, its templates
        bool hasTranslationSection = false;
        for (const auto &c : n["children"])
            hasTranslationSection |= c["section"] == "Translations";
        std::vector<Translation> found;
        if (n["section"] == "Translations")
            found = translations(config_, n["text"].get<std::string>());
        else if (n["kind"] == "pos" && !hasTranslationSection)
            found = translations(config_, n["text"].get<std::string>(), false);
        for (const auto &t : found) {
            std::string code = languageCode_(t.lang);
            json item = {{"lang", code.empty() ? json() : json(code)}, {"term", t.term}};
            if (!t.sense.empty())
                item["sense"] = t.sense;
            if (code != t.lang)
                item["lang_written"] = t.lang; // pl "angielski", it "{{en}}" -> en
            n["translations"].push_back(std::move(item));
        }
        stats.senses += n["senses"].size();
        stats.translations += n["translations"].size();
        n.erase("template_pos");
        // The text of sections handled in this phase is not repeated: "source" points into the dump
        if (!n["senses"].empty() || !n["translations"].empty() || n["section"] == "Translations")
            n.erase("text");
        for (auto &c : n["children"])
            finishNode(c, stats);
        // Sections with nothing in them (pl {{składnia}} with no text) and sections with no known
        // name (listed in unknown_headings) are left out; known sections under an unknown one stay
        auto &children = n["children"];
        for (size_t i = children.size(); i-- > 0;) {
            json &c = children[i];
            if (!c.contains("section")) {
                json lifted = c.value("children", json::array());
                children.erase(i);
                for (size_t j = 0; j < lifted.size(); ++j)
                    children.insert(children.begin() + static_cast<ptrdiff_t>(i + j), lifted[j]);
                continue;
            }
            if (!c.contains("text") && !c.contains("senses") && !c.contains("translations") &&
                !c.contains("children") && c.value("kind", json()) != "pos")
                children.erase(i);
        }
        // Empty and default fields are left out (kind follows from the section name); template
        // lines have no level of their own
        for (const char *key : {"senses", "translations", "children"})
            if (n.contains(key) && n[key].empty())
                n.erase(key);
        for (const char *key : {"index", "section"})
            if (n.contains(key) && n[key].is_null())
                n.erase(key);
        n.erase("kind");
        if (n.contains("text") && n["text"].get<std::string>().empty())
            n.erase("text");
        if (n.value("template", false))
            n.erase("level");
    }
};

// source: where the page is in the dump, [start of its bz2 block, number in it]; null for old samples
json convertPage(const std::string &wiki, const json &config, LanguageNames &names, const SectionNames &sectionNames,
                 const std::string &title, int ns, const std::string &text, Stats &stats, bool markersOnly = false,
                 const json &source = nullptr) {
    json sections = json::array();
    json *current = nullptr;
    std::optional<LanguageEntry> entry;
    const bool byName = str(config, "/language/value") == "name";
    const auto native = nativeCodes(wiki, config);
    auto finish = [&](size_t end) {
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
    // Code of a language marker: by name (lookup, strip), or a code of another scheme (hu "eng")
    auto markerCode = [&](const std::string &marker) {
        std::string code = names.code(wiki, marker, str(config, "/language/lookup"),
                                      config["language"].value("strip", std::vector<std::string>{}));
        if (code.empty())
            code = names.mappedCode(marker);
        return code;
    };
    const bool requireCode = byName && !implicit && config["language"].value("require_code", true);
    std::istringstream in(text);
    bool first = true;
    size_t offset = 0, next = 0;
    for (std::string line; std::getline(in, line); offset = next) {
        next = offset + line.size() + 1;
        auto marker = implicit ? (first ? std::optional(native.front()) : std::nullopt) : languageMarker(config, line);
        first = false;
        if (marker && requireCode && !(config.contains(json::json_pointer("/language/aliases")) &&
                                       config["language"]["aliases"].contains(*marker)) &&
            markerCode(*marker).empty()) {
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
            finish(offset);
            std::string code = byName && !implicit ? markerCode(*marker) : *marker;
            if (!byName && !implicit && !names.isCode(code))
                if (auto mapped = names.mappedCode(code); !mapped.empty())
                    code = mapped; // a code of another scheme in a code template
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
            entry.emplace(config, sectionNames, [&](const std::string &lang) {
                // Canonical code of a translation language: a code as such (ru "az.cyr": without the
                // script), a code of another scheme (ISO 639-3 "quz"), else a name of this wiki
                if (names.isCode(lang))
                    return lang;
                if (auto dot = lang.find('.'); dot != std::string::npos && names.isCode(lang.substr(0, dot)))
                    return lang.substr(0, dot);
                if (auto mapped = names.mappedCode(lang); !mapped.empty())
                    return mapped;
                std::string code = names.code(wiki, lang, str(config, "/language/lookup"),
                                              config["language"].value("strip", std::vector<std::string>{}));
                if (auto aliases = json::json_pointer("/language/aliases"); code.empty() && config.contains(aliases))
                    if (auto it = config[aliases].find(lang); it != config[aliases].end())
                        code = it->get<std::string>();
                return code;
            }, wikt2jsonl_heading(line) ? wikt2jsonl_heading(line)->level : 1);
            if (keepLine && !markersOnly)
                entry->line(line, offset);
            stats.sections++;
            stats.sectionsWithCode += !code.empty();
            continue;
        }
        if (!current || markersOnly)
            continue;
        entry->line(line, offset);
    }
    finish(text.size());
    // Pages without any language marker in mostly one-language wikis (gu, ig): all in the wiki's language
    if (sections.empty() && !markersOnly && config["language"].value("fallback_native", false) &&
        str(config, "/language/form") != "implicit") {
        json implicit = config;
        implicit["language"]["form"] = "implicit";
        return convertPage(wiki, implicit, names, sectionNames, title, ns, text, stats, markersOnly, source);
    }
    stats.pages++;
    stats.pagesWithSections += !sections.empty();
    return {{"wiki", wiki}, {"title", title}, {"ns", ns}, {"source", source}, {"sections", sections}};
}

// Output of one wiki: plain JSONL, or bz2 blocks of 100 lines (each an independent
// stream, so bzcat reads the file) with an index, tab-separated: a line per
// block "offset count uncompressed", then a line "\tstart\ttitle" per page
// (start in the uncompressed block; a page ends where the next one starts, the
// last at uncompressed), at the end EOF=<size> (a block's compressed length is
// the next offset or EOF minus its offset). Thread-safe.
class JsonlWriter {
public:
    JsonlWriter(const fs::path &base, bool compressed) : compressed_(compressed) {
        if (!compressed) {
            data_.open(base.string() + ".jsonl");
            return;
        }
        data_.open(base.string() + ".jsonl.bz2", std::ios::binary);
        index_.open(base.string() + ".jsonl.idx");
        index_ << "# wikt2jsonl index: offset\tcount\tuncompressed, then \tstart\ttitle per entry; EOF=<size>\n";
    }
    ~JsonlWriter() { close(); }

    void add(const std::string &line, const std::string &title) {
        std::lock_guard lock(mutex_);
        if (!compressed_) {
            data_ << line << '\n';
            return;
        }
        starts_.push_back(block_.size());
        block_ += line;
        block_ += '\n';
        titles_.push_back(title);
        if (titles_.size() >= blockLines)
            flush();
    }

    void close() {
        std::lock_guard lock(mutex_);
        if (!data_.is_open())
            return;
        if (compressed_) {
            flush();
            index_ << "EOF=" << offset_ << '\n';
            index_.close();
        }
        data_.close();
    }

private:
    static constexpr size_t blockLines = 100;
    bool compressed_;
    std::ofstream data_, index_;
    std::mutex mutex_;
    std::string block_;
    std::vector<std::string> titles_;
    std::vector<size_t> starts_; // of the lines in block_
    uint64_t offset_ = 0;

    void flush() {
        if (titles_.empty())
            return;
        std::string out(block_.size() + block_.size() / 100 + 600, '\0');
        auto length = static_cast<unsigned>(out.size());
        if (BZ2_bzBuffToBuffCompress(out.data(), &length, block_.data(), static_cast<unsigned>(block_.size()), 9, 0, 0) != BZ_OK)
            throw std::runtime_error("bz2 compression failed");
        data_.write(out.data(), length);
        index_ << offset_ << '\t' << titles_.size() << '\t' << block_.size() << '\n';
        for (size_t i = 0; i < titles_.size(); ++i)
            index_ << '\t' << starts_[i] << '\t' << titles_[i] << '\n';
        offset_ += length;
        block_.clear();
        titles_.clear();
        starts_.clear();
    }
};

// Pages converted from dumps: the main namespace (with translation subpages)
// and content namespaces 100-199 (thesaurus, reconstructions, appendices),
// no talk pages, no portals
bool convertedNamespace(const wikilib::dump::SiteInfo &site, int ns) {
    if (ns == 0)
        return true;
    if (ns < 100 || ns > 199 || ns % 2 == 1)
        return false;
    const auto *n = site.find_namespace(ns);
    return n && n->name != "Portal" && n->name != "Portail" && n->name != "Портал" && n->name != "Portale";
}

} // namespace

int main(int argc, char *argv[]) {
    try {
        fs::path input = "samples100", output = "jsonl", dbFile = "wiktlangs.sqlite";
        fs::path configDir = WIKTPARSE_RES_DIR "/wiktionary";
        std::vector<std::string> wikis;
        bool fromDumps = false, markersOnly = false, compress = false;
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
            else if (arg == "--bz2")
                compress = true;
            else if (arg == "-j" && i + 1 < argc)
                threads = std::max(1, std::stoi(argv[++i]));
            else if (arg == "-h" || arg == "--help") {
                std::cout << "usage: wikt2jsonl [-i samples100 | --dump [-j N]] [--markers-only] [--bz2] [-o jsonl] [--db wiktlangs.sqlite] [wiki...]\n"
                             "       --dump: all pages of the dumps, each wiki read in N threads (default 8)\n"
                             "       --markers-only: only language markers and stats, no JSONL\n"
                             "       --bz2: <wiki>.jsonl.bz2 in blocks of 100 lines with <wiki>.jsonl.idx\n";
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
            std::optional<JsonlWriter> out;
            if (!markersOnly)
                out.emplace(output / wiki, compress);
            if (fromDumps) {
                auto path = WikiDump::dumpPath(wikilib::dump::WikiProject::Wiktionary, wiki);
                wikilib::dump::DumpReader reader(path);
                auto site = reader.read_site_info();
                if (!site)
                    throw std::runtime_error(wiki + ": " + reader.error());
                std::vector<Stats> workerStats(wikilib::dump::parallel_worker_count(path, threads));
                std::cout << wiki << ": " << path.dump_path().filename().string() << std::endl;
                std::optional<wikilib::ProgressReporter> progress;
                auto error = wikilib::dump::process_parallel(
                        path, threads,
                        [&](size_t worker, const std::string &title, std::string_view content,
                            const wikilib::dump::PageLocation &location) {
                            const int ns = site->namespace_of(title);
                            if (!convertedNamespace(*site, ns))
                                return true;
                            auto line = convertPage(wiki, config, names, sectionNames, title, ns,
                                                    wikilib::markup::strip_comments(content), workerStats[worker], markersOnly,
                                                    json::array({location.block, location.index}));
                            // Pages without a recognized language are left out
                            if (out && !line["sections"].empty())
                                out->add(line.dump(-1, ' ', false, json::error_handler_t::replace), title);
                            return true;
                        },
                        [&](const wikilib::dump::DumpReader::ProcessProgress &p) {
                            if (!progress)
                                progress.emplace(p.bytes_total, 0.25);
                            progress->update(p.bytes_compressed);
                        });
                if (!error.empty())
                    throw std::runtime_error(wiki + ": " + error);
                for (const auto &w : workerStats)
                    stats.add(w);
            } else
            for (const auto &page : readJson(input / wiki / "meta.json")) {
                std::ifstream file(input / wiki / page["file"].get<std::string>());
                std::stringstream text;
                text << file.rdbuf();
                // As from dumps: without comments
                auto line = convertPage(wiki, config, names, sectionNames, page["title"], page["ns"],
                                        wikilib::markup::strip_comments(text.str()), stats, markersOnly,
                                        page.value("source", json()));
                if (out && !line["sections"].empty()) // pages without a recognized language are left out
                    out->add(line.dump(-1, ' ', false, json::error_handler_t::replace), page["title"]);
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
