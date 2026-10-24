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
// wiktlangs: resolved names into language_name) and <out>/stats.tsv (also printed): per wiki pages, sections, sections with
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
#include <shared_mutex>
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
        auto key = wiki + '\t' + name + '\t' + lookup;
        {
            std::shared_lock shared(mutex_); // threads read the cache together
            if (auto it = cache_.find(key); it != cache_.end())
                return it->second;
        }
        std::lock_guard lock(mutex_);
        if (auto it = cache_.find(key); it != cache_.end())
            return it->second;
        // The canonical code of the wiki's language, as in_code of language_name (as in wiktlangs)
        std::string lang = wiki;
        std::ranges::replace(lang, '_', '-');
        // Wiktionaries whose Wikidata label language differs
        static const std::map<std::string, std::string> labelLanguage = {
                {"no", "nb"}, {"simple", "en"}, {"zh-min-nan", "nan"}, {"roa-rup", "rup"}, {"bh", "bho"}};
        if (auto it = labelLanguage.find(lang); it != labelLanguage.end())
            lang = it->second;
        if (query("SELECT code FROM language WHERE code=?2 AND ?1<>''", "x", lang).empty()) {
            auto mapped = query("SELECT m.language FROM code_map m JOIN language l ON l.code=m.language WHERE m.scheme='wikimedia' AND m.code=?2 AND ?1<>'' ORDER BY l.kind<>'language' LIMIT 1", "x", lang);
            lang = !mapped.empty() ? mapped : lang.substr(0, lang.find('-'));
        }
        // "esperanto (morfem)", "język norweski (bokmål)": also without the note
        std::vector<std::string> variants{name};
        if (auto at = lookup.find("{name}"); at != std::string::npos)
            variants.push_back(std::string(lookup).replace(at, 6, name));
        for (const auto &word : strip)
            if (auto at = name.find(word); at != std::string::npos)
                variants.push_back(trim(std::string(name).erase(at, word.size())));
        // SQLite lower() changes ASCII letters only ("Čeština")
        for (size_t i = 0, n = variants.size(); i < n; ++i)
            if (auto lower = wikilib::unicode::to_lower(variants[i]); lower != variants[i]) {
                variants.push_back(lower);
                // az "İngilis dili": lowered "i̇ngilis", without the combining dot
                if (auto plain = withoutCombining(lower); plain != lower)
                    variants.push_back(plain);
            }
        // skr "اُردو": without vowel marks and other combining characters
        if (auto plain = withoutCombining(name); plain != name)
            variants.push_back(plain);
        if (auto open = name.rfind(" ("); open != std::string::npos && name.ends_with(")"))
            variants.push_back(name.substr(0, open));
        std::string result;
        // A code written as such ("en" from {{=en=}})
        static const std::regex code(R"(^[a-z]{2,3}(-[a-z]+)*$)");
        if (std::regex_match(name, code))
            result = query("SELECT code FROM language WHERE code=?2 AND ?1<>'' LIMIT 1", lang, name);
        // Names in the wiki's language: en.wikt names (en), Wikidata labels, aliases, then the
        // resolved headings of Wiktionaries and the aliases of the configurations; languages first
        for (const auto &n : variants) {
            if (result.empty())
                result = query("SELECT n.code FROM language_name n JOIN language l ON l.code=n.code WHERE n.in_code=?1 AND n.name=?2 AND n.source IN ('en.wikt','extra','enwiki','sil','wikidata-label','wikidata-alias','wikidata-other','wikt-heading','config-alias') ORDER BY n.source='config-alias', n.source='wikt-heading', n.source='wikidata-other', n.source='wikidata-alias', l.kind<>'language', length(n.code) LIMIT 1", lang, n);
            if (result.empty())
                result = query("SELECT n.code FROM language_name n JOIN language l ON l.code=n.code WHERE n.in_code=?1 AND lower(n.name)=lower(?2) AND n.source IN ('en.wikt','extra','enwiki','sil','wikidata-label','wikidata-alias','wikidata-other','wikt-heading','config-alias') ORDER BY n.source='config-alias', n.source='wikt-heading', n.source='wikidata-other', n.source='wikidata-alias', l.kind<>'language', length(n.code) LIMIT 1", lang, n);
        }
        // English names of en.wikt
        if (result.empty())
            // (not the wikis' own codes: their names are no English ones)
            result = query("SELECT code FROM language WHERE name=?2 AND ?1<>'' AND code NOT LIKE '%:%' ORDER BY kind<>'language' LIMIT 1", lang, name);
        // fr "proto-munda": en.wikt "Proto-Munda"
        if (result.empty())
            result = query("SELECT code FROM language WHERE lower(name)=lower(?2) AND ?1<>'' AND code NOT LIKE '%:%' ORDER BY kind<>'language' LIMIT 1",
                           lang, name);
        // Names from Lua modules of Wiktionaries in the wiki's language (wiktmodules)
        for (const auto &n : variants)
            if (result.empty())
                result = query("SELECT code FROM language_name WHERE in_code=?1 AND name=?2 AND source='wikt-module' LIMIT 1", lang, n);
        // A name that is a whole part of names of one language only: fr "eudeve" of
        // "opata-eudeve" (opt); not pl "szkocki" ("język szkocki" sco, "gaelicki szkocki" gd)
        if (const auto lower = wikilib::unicode::to_lower(name); result.empty() && lower.size() >= 4)
            result = query("SELECT CASE WHEN count(DISTINCT code) = 1 THEN max(code) END FROM language_name "
                           "WHERE in_code=?1 AND (lower(name) LIKE ?2||'-%' OR lower(name) LIKE '%-'||?2 OR "
                           "lower(name) LIKE '%-'||?2||'-%' OR lower(name) LIKE ?2||' %' OR lower(name) LIKE '% '||?2 "
                           "OR lower(name) LIKE '% '||?2||' %')",
                           lang, lower);
        return cache_[key] = result;
    }

    // Canonical code of a code of another scheme (ISO 639-1/3, Wikimedia, Glottolog)
    std::string mappedCode(const std::string &code) {
        if (!db_ || code.empty() || code.size() > 12)
            return "";
        {
            std::shared_lock shared(mutex_);
            if (auto it = mapped_.find(code); it != mapped_.end())
                return it->second;
        }
        std::lock_guard lock(mutex_);
        if (auto it = mapped_.find(code); it != mapped_.end())
            return it->second;
        return mapped_[code] = query("SELECT m.language FROM code_map m JOIN language l ON l.code=m.language WHERE m.code=?2 AND ?1<>'' ORDER BY l.kind<>'language', m.method='retired' LIMIT 1", "x", code);
    }

    // A Wiktionary's own code (fr "proto-khasique", "eudeve"): a known language or
    // <wiki>:<code> (code_map, scheme wikt-<wiki>); empty for none
    std::string wikiCode(const std::string &wiki, const std::string &code) {
        if (!db_ || code.empty())
            return "";
        const auto key = wiki + '\t' + code;
        {
            std::shared_lock shared(mutex_);
            if (auto it = wikiCodes_.find(key); it != wikiCodes_.end())
                return it->second;
        }
        std::lock_guard lock(mutex_);
        if (auto it = wikiCodes_.find(key); it != wikiCodes_.end())
            return it->second;
        return wikiCodes_[key] = query("SELECT language FROM code_map WHERE scheme='wikt-'||?1 AND code=?2 LIMIT 1", wiki, code);
    }

    // A language code of wiktlangs.sqlite (en.wikt codes and extra ones)
    bool isCode(const std::string &code) {
        if (!db_ || code.empty())
            return false;
        {
            std::shared_lock shared(mutex_);
            if (auto it = codes_.find(code); it != codes_.end())
                return it->second;
        }
        std::lock_guard lock(mutex_);
        if (auto it = codes_.find(code); it != codes_.end())
            return it->second;
        return codes_[code] = !query("SELECT code FROM language WHERE code=?2 AND ?1<>'' LIMIT 1", "x", code).empty();
    }

private:
    sqlite3 *db_ = nullptr;
    std::map<std::string, std::string> cache_;
    std::map<std::string, bool> codes_;
    std::map<std::string, std::string> mapped_, wikiCodes_;
    std::shared_mutex mutex_; // shared: cache hits; exclusive: a query (one SQLite connection)

    std::string query(const char *sql, const std::string &a, const std::string &b) {
        sqlite3_stmt *stmt = nullptr;
        std::string result;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, a.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, b.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW)
                if (const auto text = sqlite3_column_text(stmt, 0)) // NULL: none
                    result = reinterpret_cast<const char *>(text);
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
// The code captured by language.template_regex from a template name, if it matches
std::optional<std::string> templateRegexCode(const json &language, const std::string &name) {
    if (!language.contains("template_regex"))
        return std::nullopt;
    std::smatch m;
    // Compiled once per pattern and thread
    thread_local std::map<std::string, std::regex> compiled;
    const auto &pattern = language["template_regex"].get_ref<const std::string &>();
    auto it = compiled.find(pattern);
    if (it == compiled.end())
        it = compiled.emplace(pattern, std::regex(pattern)).first;
    if (std::regex_match(name, m, it->second) && m.size() > 1)
        return m[1].str();
    return std::nullopt;
}

std::optional<std::string> languageMarker(const json &config, const std::string &line) {
    const auto &language = config["language"];
    if (language.contains("parameter") || language.contains("template_regex"))
        if (auto t = templateLine(line)) {
            if (language.contains("parameter"))
                if (auto v = t->get(language["parameter"].get<std::string>()); v && !v->empty())
                    return std::string(*v);
            if (auto code = templateRegexCode(language, t->name))
                return code;
        }
    // The regex also on a marker of a heading: mg =={{=mg=}}== -> mg
    if (auto marker = languageMarkerOfForm(config, line))
        return templateRegexCode(language, *marker).value_or(*marker);
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
    // "line": every text line of the definitions (lo ===ຄວາມໝາຍ=== without marks)
    if (marker == "line") {
        auto text = trim(line);
        if (text.empty() || text.starts_with("{{") || text.starts_with("[[Category:") || text.starts_with("*") ||
            text.starts_with("<") || text.starts_with("|") || text.starts_with("}}"))
            return std::nullopt;
        while (!text.empty() && (text[0] == ':' || text[0] == '#' || text[0] == '*')) // lo ":*text"
            text = trim(text.substr(1));
        static const std::regex number(R"(^\d+[.)]?\s+(.*)$)"); // uz "1 Biror oila…"
        if (std::smatch n; std::regex_match(text, n, number))
            text = n[1].str();
        if (text.empty())
            return std::nullopt;
        // Placeholders of empty entries (lo "ກະລຸນາຊ່ອຍຂຽນ…": please write)
        if (auto ignore = json::json_pointer("/senses/ignore"); config.contains(ignore))
            for (const auto &prefix : config[ignore])
                if (text.starts_with(prefix.get<std::string>()))
                    return std::nullopt;
        counters.resize(1, 0);
        return Sense{std::to_string(++counters[0]), text};
    }
    // "#", ":" of lines indented as such (lo ": definition"), ":#" of mn (":#definition")
    if (marker.starts_with("#") || marker == ":" || marker == "*" || marker == ":#") { // te "*"
        if (marker == ":#" && !line.starts_with(":#"))
            return std::nullopt;
        if (marker == ":#")
            return senseLine({{"senses", {{"marker", "#"}}}}, line.substr(1), counters);
        const char c = marker[0];
        size_t depth = 0;
        while (depth < line.size() && line[depth] == c)
            ++depth;
        if (depth == 0 || (depth < line.size() && (line[depth] == ':' || line[depth] == '*' || line[depth] == '#')))
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
    std::string gloss; // of the table (en {{trans-top|domestic species}})
    bool generic = false; // not by the configuration: kept only when its language is known
    std::vector<std::string> gender; // de {{Ü|de|Hund}} {{m}}, en {{t|de|Hund|m}}
    std::string tr;                  // transliteration: en {{t|ru|вода|tr=voda}}
};

// A gender or number mark: m, f, n, c, m-p, pl ż (żeński), mf
bool genderMark(std::string_view s) {
    static const std::regex mark(R"(^(m|f|n|c|ż|mf|fm|p|pl|du|s|sg)(-?(p|pl|s|sg|d|du|an|in))?$)");
    return !s.empty() && s.size() < 8 && std::regex_match(std::string(s), mark);
}

// Gender and transliteration of a translation template: params after the term, g=, g2=, tr=
void templateExtras(const TemplateInvocation &t, size_t termParam, Translation &item) {
    for (size_t i = termParam + 1; i < termParam + 5; ++i)
        if (auto v = t.get(i); v && genderMark(trim(std::string(*v))))
            item.gender.push_back(trim(std::string(*v)));
    for (const char *key : {"g", "g1", "g2", "g3", "gen"})
        if (auto v = t.get(std::string(key)); v && !trim(std::string(*v)).empty())
            item.gender.push_back(trim(std::string(*v)));
    for (const char *key : {"tr", "ts", "translit", "R"})
        if (auto v = t.get(std::string(key)); v && !trim(std::string(*v)).empty()) {
            item.tr = trim(std::string(*v));
            break;
        }
}

// Gender templates of a piece of a translation line: "[[Hund]] {{m}}"
std::vector<std::string> lineGenders(const std::string &piece) {
    std::vector<std::string> result;
    for (const auto &t : wikilib::templates::find_invocations(piece))
        if (t.parameters.empty() && genderMark(trim(t.name)))
            result.push_back(trim(t.name));
    return result;
}

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

// Names of sound and picture files in wikitext, in order, once each
std::vector<std::string> mediaFiles(const std::string &text) {
    static const std::regex file(
            R"(([^\[\]{}|=\n:<>]+\.(?:ogg|oga|wav|mp3|flac|opus|webm|mid|jpg|jpeg|png|svg|gif|tif|tiff|webp)))",
            std::regex::icase);
    std::vector<std::string> result;
    for (std::sregex_iterator it(text.begin(), text.end(), file), end; it != end; ++it)
        if (auto name = trim((*it)[1].str()); !name.empty() && !std::ranges::contains(result, name))
            result.push_back(name);
    return result;
}

// Links of a translation line with the genders of their piece: "[[Hund]] {{m}}, [[Rüde]] {{m}}"
// (pieces split at , and ; outside [[…]] and {{…}})
std::vector<std::pair<std::string, std::vector<std::string>>> linkPieces(const std::string &text) {
    std::vector<std::pair<std::string, std::vector<std::string>>> result;
    std::vector<std::string> pieces(1);
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if ((c == '[' || c == '{') && i + 1 < text.size() && text[i + 1] == c)
            ++depth;
        else if ((c == ']' || c == '}') && i + 1 < text.size() && text[i + 1] == c && depth > 0)
            --depth;
        if (depth == 0 && (c == ',' || c == ';'))
            pieces.emplace_back();
        else
            pieces.back() += c;
    }
    for (const auto &piece : pieces) {
        const auto genders = lineGenders(piece);
        for (const auto &term : links(piece))
            result.emplace_back(term, genders);
    }
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
    std::string tableGloss; // gloss of the current table (en, sv)
    for (const auto &t : allTemplates(text)) {
        if (table.is_object() && table.contains("top") &&
            std::ranges::contains(table["top"].get<std::vector<std::string>>(), t.name)) {
            tableSense.clear();
            tableGloss.clear();
            if (table.contains("number") && table["number"].is_number_integer())
                if (auto v = t.get(static_cast<size_t>(table["number"].get<int>())))
                    tableSense = trim(std::string(*v));
            // gloss: a positional parameter (1) or a named one (de "G")
            if (const auto g = table.value("gloss", json()); g.is_number_integer()) {
                if (auto v = t.get(static_cast<size_t>(g.get<int>())))
                    tableGloss = trim(std::string(*v));
            } else if (g.is_string())
                if (auto v = t.get(g.get<std::string>()))
                    tableGloss = trim(std::string(*v));
            // ru: {{перев-блок|gloss|en={{t|en|water}}|ab=[[аӡы]]}}
            if (table.value("items", std::string()) == "code-parameters")
                for (const auto &[key, value] : t.parameters)
                    if (!key.empty() && value.find("{{") == std::string::npos)
                        for (const auto &term : links(value))
                            result.push_back({key, term, tableSense, tableGloss});
            continue;
        }
        if (std::ranges::find(names, t.name) == names.end())
            continue;
        auto lang = t.get(static_cast<size_t>(langParam));
        if (!lang)
            continue;
        if (termParam.is_number_integer()) {
            if (auto term = t.get(static_cast<size_t>(termParam.get<int>())); term && !term->empty()) {
                Translation item{std::string(*lang), std::string(*term), tableSense, tableGloss};
                templateExtras(t, static_cast<size_t>(termParam.get<int>()), item);
                result.push_back(std::move(item));
            }
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
                result.push_back({std::string(*lang), std::string(*term), sense, tableGloss});
            }
        }
    }
    // Genders as templates after a translation template: de "{{Ü|de|Hund}} {{m}}", up to the next , or ;
    if (!names.empty() && termParam.is_number_integer()) {
        std::istringstream in(text);
        for (std::string l; std::getline(in, l);) {
            if (l.find("{{") == std::string::npos)
                continue;
            auto all = wikilib::templates::find_invocations(l);
            for (size_t i = 0; i < all.size(); ++i) {
                if (!std::ranges::contains(names, all[i].name))
                    continue;
                const size_t from = all[i].location.end.offset;
                size_t to = l.size();
                for (size_t j = i + 1; j < all.size(); ++j)
                    if (std::ranges::contains(names, all[j].name)) {
                        to = all[j].location.begin.offset;
                        break;
                    }
                auto after = l.substr(from, to - from);
                after = after.substr(0, after.find_first_of(",;"));
                auto genders = lineGenders(after);
                auto lang = all[i].get(static_cast<size_t>(langParam));
                auto term = all[i].get(static_cast<size_t>(termParam.get<int>()));
                if (genders.empty() || !lang || !term)
                    continue;
                for (auto &item : result)
                    if (item.gender.empty() && item.lang == *lang && item.term == *term) {
                        item.gender = genders;
                        break;
                    }
            }
        }
    }
    // Lines of links: pl "* angielski: (1.1) [[water]]", it ":*{{en}}: [[water]]", zh "*英语：[[water]]"
    if (lines && terms.contains("line")) {
        // std::regex works on bytes: the full-width colon (3 bytes in UTF-8) is an
        // alternative, not a member of a bracket ([:：] would take its bytes one by
        // one, and ż, ü, whose second byte is one of them, would end a name)
        static const std::regex line(R"(^:?\*+:?\s*(\{\{([^}|]+)\}\}|(?:(?!：)[^:])+?)\s*(?::|：)\s*(?:\(([0-9.,\s-]+)\)\s*)?(.*)$)");
        // The table a line is in: its number and gloss, from the parameters of its top
        // template on the line (fi "{{kohta|1|H₂O|" is closed lines below)
        std::vector<std::string> tops;
        if (table.is_object() && table.contains("top"))
            tops = table["top"].get<std::vector<std::string>>();
        const std::string bottom = table.is_object() ? table.value("bottom", std::string()) : std::string();
        std::string lineSense, lineGloss;
        std::istringstream in(text);
        for (std::string l; std::getline(in, l);) {
            for (size_t at = l.find("{{"); at != std::string::npos; at = l.find("{{", at + 2)) {
                const auto end = l.find("}}", at);
                std::vector<std::string> fields;
                std::stringstream parts(l.substr(at + 2, end == std::string::npos ? std::string::npos : end - at - 2));
                for (std::string f; std::getline(parts, f, '|');)
                    fields.push_back(trim(f));
                if (fields.empty())
                    continue;
                if (fields[0] == bottom) {
                    lineSense.clear();
                    lineGloss.clear();
                } else if (std::ranges::contains(tops, fields[0])) {
                    auto field = [&](const json &index) {
                        return index.is_number_integer() && size_t(index.get<int>()) < fields.size()
                                       ? fields[size_t(index.get<int>())]
                                       : std::string();
                    };
                    lineSense = field(table.value("number", json()));
                    lineGloss = field(table.value("gloss", json()));
                }
            }
            std::smatch m;
            if (!std::regex_match(l, m, line))
                continue;
            std::string lang = m[2].matched ? m[2].str() : trim(m[1].str());
            for (const auto &[term, genders] : linkPieces(m[4].str())) {
                Translation item{lang, term, m[3].matched && m[3].length() ? m[3].str() : lineSense, lineGloss};
                item.gender = genders;
                result.push_back(std::move(item));
            }
        }
    }
    // A translation section the configuration finds nothing in: any template of a code and a term
    // (ug {{ter|en|water}}, is {{þýðing|af|Mali}}, hsb {{p|dsb|a}}), else lines of links
    // (kk "#**Ағылшынша: [[home]]", tk "*{{en}}: [[water]]"); kept only of known languages
    if (lines && result.empty()) {
        static const std::regex code(R"(^[a-z]{2,3}(-[a-z]{2,8})*$)");
        for (const auto &t : allTemplates(text)) {
            auto lang = t.get(size_t{1});
            auto term = t.get(size_t{2});
            if (lang && term && std::regex_match(std::string(*lang), code) && !trim(std::string(*term)).empty() &&
                std::string(*term).find("{{") == std::string::npos) {
                Translation item{std::string(*lang), trim(std::string(*term)), "", "", true};
                templateExtras(t, 2, item);
                result.push_back(std::move(item));
            }
        }
        if (result.empty()) {
            static const std::regex line(R"(^[:#]*\*+:?\s*(\{\{([^}|]+)\}\}|(?:(?!：)[^:])+?)\s*(?::|：)\s*(.*)$)");
            std::istringstream in(text);
            for (std::string l; std::getline(in, l);) {
                std::smatch m;
                if (!std::regex_match(l, m, line))
                    continue;
                std::string lang = m[2].matched ? m[2].str() : trim(m[1].str());
                for (const auto &[term, genders] : linkPieces(m[3].str())) {
                    Translation item{lang, term, "", "", true};
                    item.gender = genders;
                    result.push_back(std::move(item));
                }
            }
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
        "inflected form", "noun form", "verb form", "adjective form", "adverb form", "pronoun form", "participle form",
        "romanization", "part of speech" /* kk ===Сөз табы===: the part of speech as a line, the senses */};
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
// Section names of res/wiktionary_section_names.tsv by wiki: lower-case name -> section,
// read directly, so that a change there needs no new wiktsections.sqlite
std::map<std::string, std::vector<std::pair<std::string, std::string>>> manualSectionNames;

class SectionNames {
public:
    SectionNames(const fs::path &file, const std::string &wiki) {
        if (auto it = manualSectionNames.find(wiki); it != manualSectionNames.end())
            for (const auto &[name, section] : it->second)
                names_.emplace(name, section);
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

    // compound: of a {{-…-}} template, whose name may have a language code (is {{-is-nafnorð-}},
    // nah {{-noun-nci-}}); not of others ({{it-flexion}}, {{sb-plural}})
    // Headword templates (shn {{shn-noun}}, om {{En-noun}}): a part of speech after a code only
    [[nodiscard]] std::optional<SectionName> find(const std::string &key, int compound = 0) const {
        auto it = names_.find(wikilib::unicode::to_lower(trim(key)));
        if (const auto dash = key.find('-'); compound && it == names_.end() && dash != std::string::npos) {
            static const std::regex code(R"(^[A-Za-z]{2,3}$)");
            const auto first = key.substr(0, dash), last = key.substr(key.rfind('-') + 1);
            if (auto rest = names_.find(wikilib::unicode::to_lower(trim(key.substr(dash + 1))));
                rest != names_.end() && (compound == 1 || std::regex_match(first, code)))
                it = rest;
            else if (auto head = names_.find(wikilib::unicode::to_lower(trim(key.substr(0, key.rfind('-')))));
                     head != names_.end() && (compound == 1 || std::regex_match(last, code)))
                it = head;
            if (it != names_.end() && compound == 2 && !partsOfSpeech.contains(wikilib::unicode::to_lower(it->second)))
                it = names_.end();
        }
        if (it == names_.end()) {
            // Abbreviations of parts of speech in templates of many wikis: id {{-n-|id}}, an {{-adj-}}
            static const std::map<std::string, std::string> abbreviations = {
                    {"n", "noun"}, {"v", "verb"}, {"adj", "adjective"}, {"adv", "adverb"}, {"pron", "pronoun"},
                    {"num", "numeral"}, {"prep", "preposition"}, {"conj", "conjunction"}, {"interj", "interjection"},
                    {"suf", "suffix"}, {"pref", "prefix"}, {"propn", "proper noun"}, {"prop", "proper noun"},
                    {"noun.masc", "noun"}, {"noun.fem", "noun"}, {"noun.neut", "noun"}, {"phr", "phrase"},
                    {"subst", "noun"}, {"sb", "noun"},
                    // and of other sections: vec {{-etim-}}, scn {{-hyph-}}, da {{-decl-}}
                    {"etym", "etymology"}, {"etim", "etymology"}, {"ety", "etymology"}, {"etymo", "etymology"},
                    {"ref", "references"}, {"hyph", "hyphenation"}, {"trans", "translations"},
                    {"trad", "translations"}, {"pronun", "pronunciation"}, {"phon", "pronunciation"},
                    {"phono", "pronunciation"}, {"decl", "declension"}, {"relat", "related terms"},
                    {"syn", "synonyms"}, {"ant", "antonyms"}};
            const auto a = abbreviations.find(wikilib::unicode::to_lower(trim(key)));
            if (a == abbreviations.end() || (it = names_.find(a->second)) == names_.end())
                return std::nullopt;
        }
        // "Synonyms", "references" (by hand): English capitalized
        std::string section = it->second;
        if (section == "Translation" || section == "translation")
            section = "Translations"; // a singular label of Wikidata (pt "Tradução")
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
        static const std::set<std::string> byParameter = {"S", "Wortart", "-pos-", "H"}; // wa {{H|sustantif|wa}}
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

// How a key may be compound (SectionNames::find): 1 of a {{-…-}} template (is {{-is-nafnorð-}}),
// 2 of another template (headwords {{shn-noun}}), 0 of no template
int dashTemplate(const std::string &text) {
    auto t = onlyTemplate(trim(text));
    if (!t)
        return 0;
    return t->name.size() > 2 && t->name.starts_with('-') && t->name.ends_with('-') ? 1 : 2;
}

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
        // The next line of an example whose template goes on: en "#* {{quote-book|en|…" "|passage=…" "}}"
        if (exampleDepth_ > 0) {
            if (auto &senses = current()["senses"]; !senses.empty() && senses.back().contains("examples") &&
                                                    !wikt2jsonl_heading(line)) {
                auto &last = senses.back()["examples"].back();
                last = last.get<std::string>() + '\n' + line;
                exampleDepth_ += templateDepth(line);
                return;
            }
            exampleDepth_ = 0;
        }
        if (auto h = wikt2jsonl_heading(line)) {
            open(h->text, h->level, false);
            return;
        }
        // sr: a section as a template over lines: {{Значење|, "# …", }}
        if (config_.contains(json::json_pointer("/sections/block_templates"))) {
            static const std::regex blockStart(R"(^\{\{([^|{}]+)\|\s*$)");
            std::smatch m;
            if (std::regex_match(line, m, blockStart)) { // also unknown ones ({{Примери|): not definitions
                open("{{" + trim(m[1].str()) + "}}", headingLevel() + 1, true);
                return;
            }
            if (trim(line) == "}}" && current().value("template", false))
                return;
        }
        // A template line with the name of a section; where sections are headings only a
        // {{-…-}} one or one without parameters (an, co {{-noun-}}; not es {{sinónimo|…}}
        // of a definition); not {{trans-top|…}}, no known name
        if (auto t = templateLine(line);
            t && t->name.size() < 40 &&
            (str(config_, "/sections/form") != "heading" || t->parameters.empty() ||
             (t->name.size() > 2 && t->name.starts_with('-') && t->name.ends_with('-'))) &&
            names_.find(sectionKey(trim(line)).first, dashTemplate(line))) {
            open(trim(line), headingLevel() + 1, true); // {{synonimy}}, {{-sin-}}
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
                if (!s->text.empty()) // not the empty "#" and ":(1.1)" of unwritten entries
                    current()["senses"].push_back({{"id", s->id}, {"text", s->text}});
                return;
            }
        // Examples and quotations of the last sense: "#:", "#*", "##:" (en, fr, it…); "#*:" the
        // text of a quotation, after its source
        if (auto &senses = current()["senses"]; !senses.empty() && line.starts_with('#')) {
            size_t depth = 0;
            while (depth < line.size() && line[depth] == '#')
                ++depth;
            if (depth < line.size() && (line[depth] == ':' || line[depth] == '*')) {
                size_t start = depth;
                while (start < line.size() && (line[start] == ':' || line[start] == '*'))
                    ++start;
                if (auto example = trim(line.substr(start)); !example.empty()) {
                    senses.back()["examples"].push_back(example);
                    exampleDepth_ = templateDepth(example);
                }
                return;
            }
        }
        current()["text"] = current()["text"].get<std::string>() + line + '\n';
    }

    json finish(Stats &stats) {
        finishNode(root_, stats);
        json result = {{"sections", root_.value("children", json::array())}};
        if (root_.contains("text") && !root_["text"].get<std::string>().empty())
            result["text"] = root_["text"];
        if (root_.contains("media"))
            result["media"] = root_["media"]; // pictures before the first section
        if (!unknown_.empty())
            result["unknown_headings"] = unknown_;
        return result;
    }

private:
    const json &config_;
    const SectionNames &names_;
    LanguageCode languageCode_;
    json root_;
    int exampleDepth_ = 0; // templates the last example leaves open

    // {{ less }} of a text
    static int templateDepth(const std::string &text) {
        int depth = 0;
        for (size_t i = 0; i + 1 < text.size(); ++i)
            if (text[i] == text[i + 1] && (text[i] == '{' || text[i] == '}')) {
                depth += text[i] == '{' ? 1 : -1;
                ++i;
            }
        return depth;
    }
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
        auto name = names_.find(key, dashTemplate(raw));
        // he ==הָלַךְ==: a heading of this level is the entry word, its definitions below
        if (!name && level == num(config_, "/senses/entry_level", 0))
            name = SectionName{"Definitions", "section"};
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
            if (t.generic && code.empty())
                continue;
            json item = {{"lang", code.empty() ? json() : json(code)}, {"term", t.term}};
            if (!t.sense.empty())
                item["sense"] = t.sense;
            if (!t.gloss.empty())
                item["gloss"] = t.gloss;
            if (!t.gender.empty())
                item["gender"] = t.gender;
            if (!t.tr.empty())
                item["tr"] = t.tr;
            if (code != t.lang)
                item["lang_written"] = t.lang; // pl "angielski", it "{{en}}" -> en
            n["translations"].push_back(std::move(item));
        }
        stats.senses += n["senses"].size();
        stats.translations += n["translations"].size();
        n.erase("template_pos");
        // The text of sections handled in this phase is not repeated: "source" points into the dump
        // Files of Wikimedia Commons: sounds of the pronunciation, pictures ([[Plik:Pl-woda.ogg]],
        // {{audio|De-rot.ogg}}), without the namespace, which every wiki names in its language
        if (auto files = mediaFiles(n.value("text", std::string())); !files.empty())
            n["media"] = files;
        // (of a part of speech the rest of the text stays: its headword line, inflection templates)
        if (!n["translations"].empty() || n["section"] == "Translations" ||
            trim(n.value("text", std::string())).empty())
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
        // A code marker that is no code is no language: E {{-noun-}} besides {{-scn-}}, ga {{-fuaim-}}
        // after {{t|et}}: a section of the open language. Before any language it opens one: the
        // code at its end (nah {{-noun-nci-}}), else the wiki's (vo {{-pedefom-}}, gl {{-topon-|gl}}).
        // (only of the dash forms: names as markers of other forms keep their pages)
        bool sectionLine = false;
        if (marker && !byName && !implicit && config["language"].value("require_code", true) &&
            (str(config, "/language/form") == "line-template" ||
             str(config, "/language/form") == "heading-dash-template" ||
             dashTemplate(line) == 1) && // ug {{-noun-}} of a form of language.also
            !(config.contains(json::json_pointer("/language/aliases")) && config["language"]["aliases"].contains(*marker)) &&
            !names.isCode(*marker) && names.wikiCode(wiki, *marker).empty() && names.mappedCode(*marker).empty() &&
            !(marker->size() > 2 && marker->front() == '-' && marker->back() == '-' &&
              names.isCode(marker->substr(1, marker->size() - 2))) &&
            (current || sectionNames.find(sectionKey(*marker).first, 1) || templateLine(line))) {
            if (current) {
                marker.reset();
            } else {
                auto dash = marker->rfind('-');
                auto tail = dash == std::string::npos ? std::string() : marker->substr(dash + 1);
                marker = names.isCode(tail) ? tail : native.front();
                sectionLine = true;
            }
        }
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
            if (!byName && !implicit && !names.isCode(code)) {
                if (code.size() > 2 && code.front() == '-' && code.back() == '-' &&
                    names.isCode(code.substr(1, code.size() - 2)))
                    code = code.substr(1, code.size() - 2); // su =={{-en-}}== besides =={{sunda}}==
                else if (auto own = names.wikiCode(wiki, code); !own.empty())
                    code = own; // the wiki's own code (fr proto-khasique)
                else if (auto mapped = names.mappedCode(code); !mapped.empty())
                    code = mapped; // a code of another scheme in a code template
                else if (auto named = markerCode(code); !named.empty())
                    code = named; // a name in a code template (btm {{Mandailing}}, az {{Dil|Azərbaycan dili}})
            }
            const bool keepLine = (implicit && sections.empty() && !line.empty()) || sectionLine;
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
                if (auto own = names.wikiCode(wiki, lang); !own.empty())
                    return own; // the wiki's own code (fr proto-khasique, eudeve)
                if (auto dot = lang.find('.'); dot != std::string::npos && names.isCode(lang.substr(0, dot)))
                    return lang.substr(0, dot);
                if (auto mapped = names.mappedCode(lang); !mapped.empty())
                    return mapped;
                // ko "영어(en)": a name with its code in brackets
                static const std::regex bracketCode(R"(^.*\(([a-z]{2,3}(?:-[a-zA-Z]+)*)\)$)");
                if (std::smatch m; std::regex_match(lang, m, bracketCode) && names.isCode(m[1].str()))
                    return m[1].str();
                // A code with a script, region or variety (cu-Cyrl, zh-cn, gcf-mq): its language
                if (auto dash = lang.find('-'); dash != std::string::npos && names.isCode(lang.substr(0, dash)))
                    return lang.substr(0, dash);
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

// Output of one wiki: plain JSONL, or bz2 blocks of 100 lines or 512 KB (each an independent
// stream, so bzcat reads the file) with an index, colon-separated: a line per
// block "offset:count:uncompressed", then a line ":start:title" per page
// (start in the uncompressed block; a page ends where the next one starts, the
// last at uncompressed), at the end EOF=<size> (a block's compressed length is
// the next offset or EOF minus its offset). Thread-safe: each worker fills a block of its
// own (pages of one part of the dump, not interleaved with other threads' pages), compresses
// it outside the lock and appends it; blocks need no order, the index tells where pages are.
class JsonlWriter {
public:
    JsonlWriter(const fs::path &base, bool compressed, size_t workers = 1)
        : compressed_(compressed), blocks_(std::max<size_t>(workers, 1)) {
        if (!compressed) {
            data_.open(base.string() + ".jsonl");
            return;
        }
        data_.open(base.string() + ".jsonl.bz2", std::ios::binary);
        index_.open(base.string() + ".jsonl.idx");
        index_ << "# wikt2jsonl index: offset:count:uncompressed, then :start:title per entry; EOF=<size>\n";
    }
    ~JsonlWriter() { close(); }

    // worker: the calling thread's number, below the workers of the constructor
    void add(size_t worker, const std::string &line, const std::string &title) {
        if (!compressed_) {
            std::lock_guard lock(mutex_);
            data_ << line << '\n';
            return;
        }
        auto &block = blocks_.at(worker); // only this worker's: no lock
        block.starts.push_back(block.text.size());
        block.text += line;
        block.text += '\n';
        block.titles.push_back(title);
        if (block.titles.size() >= blockLines || block.text.size() >= blockBytes)
            write(std::exchange(block, {}));
    }

    // After all workers are done
    void close() {
        if (!data_.is_open())
            return;
        if (compressed_) {
            for (auto &block : blocks_)
                if (!block.titles.empty())
                    write(std::exchange(block, {}));
            index_ << "EOF=" << offset_ << '\n';
            index_.close();
        }
        data_.close();
    }

private:
    static constexpr size_t blockLines = 100;
    static constexpr size_t blockBytes = 512 * 1024; // uncompressed: a page read decompresses little
    struct Block {
        std::string text;
        std::vector<std::string> titles;
        std::vector<size_t> starts; // of the lines in text
    };
    bool compressed_;
    std::ofstream data_, index_;
    std::mutex mutex_;          // of the files
    std::vector<Block> blocks_; // the block being filled, per worker
    uint64_t offset_ = 0;

    void write(Block block) {
        std::string out(block.text.size() + block.text.size() / 100 + 600, '\0');
        auto length = static_cast<unsigned>(out.size());
        if (BZ2_bzBuffToBuffCompress(out.data(), &length, block.text.data(), static_cast<unsigned>(block.text.size()), 9,
                                     0, 0) != BZ_OK)
            throw std::runtime_error("bz2 compression failed");
        std::lock_guard lock(mutex_);
        data_.write(out.data(), length);
        index_ << offset_ << ':' << block.titles.size() << ':' << block.text.size() << '\n';
        for (size_t i = 0; i < block.titles.size(); ++i)
            index_ << ':' << block.starts[i] << ':' << block.titles[i] << '\n';
        offset_ += length;
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
        for (int i = argc == 1 ? 0 : 1; i < argc; ++i) {
            // No arguments: the usage, no run by accident over an earlier output
            std::string arg = argc == 1 ? "--help" : argv[i];
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
                             "       --bz2: <wiki>.jsonl.bz2 in blocks of 100 lines or 512 KB with <wiki>.jsonl.idx\n";
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
            if (f.size() >= 2 && f[0] != "wiki" && !f[0].starts_with("#")) {
                knownSections[f[0]].insert(f[1]);
                if (f.size() >= 3)
                    manualSectionNames[f[0]].emplace_back(wikilib::unicode::to_lower(f[1]), f[2]);
            }
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
            const size_t workers =
                    fromDumps ? wikilib::dump::parallel_worker_count(
                                        WikiDump::dumpPath(wikilib::dump::WikiProject::Wiktionary, wiki), threads)
                              : 1;
            std::optional<JsonlWriter> out;
            if (!markersOnly)
                out.emplace(output / wiki, compress, workers);
            if (fromDumps) {
                auto path = WikiDump::dumpPath(wikilib::dump::WikiProject::Wiktionary, wiki);
                wikilib::dump::DumpReader reader(path);
                auto site = reader.read_site_info();
                if (!site)
                    throw std::runtime_error(wiki + ": " + reader.error());
                std::vector<Stats> workerStats(workers);
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
                                out->add(worker, line.dump(-1, ' ', false, json::error_handler_t::replace), title);
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
                    out->add(0, line.dump(-1, ' ', false, json::error_handler_t::replace), page["title"]);
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
