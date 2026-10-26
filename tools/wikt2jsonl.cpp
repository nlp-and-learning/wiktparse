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
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <tuple>
#include <unordered_map>
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
#include <wikilib/core/text_utils.hpp>
#include <wikilib/core/unicode_utils.h>
#include <wikilib/templates/template_parser.h>

#include <bzlib.h>
#include <zdict.h>
#include <zstd.h>
#include <wikilib/core/progress.h>
#include <wikilib/dump/dump_reader.h>
#include <wikilib/dump/parallel_reader.h>
#include <wikilib/lua/lua_data.h>
#include <wikilib/markup/tokenizer.h>

#include "dump/WikiDump.h"
#include "util/textUtils.h"
#include "jsonl_blocks.h"

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
    explicit LanguageNames(const fs::path &file) : file_(file) {
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
        // A miss: the queries without the lock, on this thread's connection
        // The canonical code of the wiki's language, as in_code of language_name (as in wiktlangs)
        std::string lang = wiki;
        std::ranges::replace(lang, '_', '-');
        // Wiktionaries whose Wikidata label language differs
        static const std::map<std::string, std::string> labelLanguage = {
                {"no", "nb"}, {"simple", "en"}, {"zh-min-nan", "nan"}, {"roa-rup", "rup"}, {"bh", "bho"}};
        if (auto it = labelLanguage.find(lang); it != labelLanguage.end())
            lang = it->second;
        if (!isCode(lang)) {
            auto mapped = query("SELECT m.language FROM code_map m JOIN language l ON l.code=m.language WHERE m.scheme='wikimedia' AND m.code=?2 AND ?1<>'' ORDER BY l.kind<>'language' LIMIT 1", "x", lang);
            lang = !mapped.empty() ? mapped : lang.substr(0, lang.find('-'));
        }
        // "esperanto (morfem)", "język norweski (bokmål)": also without the note
        std::vector<std::string> variants{name};
        // the name cleaned: without vowel marks (ar "عَرَبِيَّة"), without a word as "language"
        // (ar "لغة إنجليزية"), and with the lookup pattern (ar "ال{name}") on each of these
        std::vector<std::string> bases{name};
        if (auto plain = withoutCombining(name); plain != name)
            bases.push_back(plain);
        for (size_t i = 0, n = bases.size(); i < n; ++i)
            for (const auto &word : strip)
                if (auto at = bases[i].find(word); at != std::string::npos)
                    bases.push_back(trim(std::string(bases[i]).erase(at, word.size())));
        for (size_t i = 1; i < bases.size(); ++i)
            variants.push_back(bases[i]);
        if (auto at = lookup.find("{name}"); at != std::string::npos)
            for (const auto &base : bases)
                variants.push_back(std::string(lookup).replace(at, 6, base));
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
        if (std::regex_match(name, code) && isCode(name))
            result = name;
        // Of the names in memory (no SQLite scans): names in the wiki's language, en.wikt
        // names (en), Wikidata labels, aliases, then the resolved headings of Wiktionaries and
        // the aliases of the configurations; languages first
        const NameIndex &index = nameIndex(lang);
        const NameIndex &english = nameIndex("");
        auto find = [](const std::unordered_map<std::string, std::string> &map, const std::string &k) {
            auto it = map.find(k);
            return it == map.end() ? std::string() : it->second;
        };
        for (const auto &n : variants) {
            if (result.empty())
                result = find(index.exact, n);
            if (result.empty())
                result = find(index.lower, wikilib::unicode::to_lower(n));
        }
        // English names of en.wikt (not the wikis' own codes: their names are no English ones);
        // fr "proto-munda": en.wikt "Proto-Munda"
        if (result.empty())
            result = find(english.exact, name);
        if (result.empty())
            result = find(english.lower, wikilib::unicode::to_lower(name));
        // Names from Lua modules of Wiktionaries in the wiki's language (wiktmodules)
        for (const auto &n : variants)
            if (result.empty())
                result = find(index.module, n);
        // A name that is a whole part of names of one language only: fr "eudeve" of
        // "opata-eudeve" (opt); not pl "szkocki" ("język szkocki" sco, "gaelicki szkocki" gd)
        if (const auto lower = wikilib::unicode::to_lower(name); result.empty() && lower.size() >= 4)
            if (auto part = find(index.part, lower); part != ambiguous)
                result = part;
        std::lock_guard lock(mutex_);
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
        auto result = query("SELECT m.language FROM code_map m JOIN language l ON l.code=m.language WHERE m.code=?2 AND ?1<>'' ORDER BY l.kind<>'language', m.method='retired' LIMIT 1", "x", code);
        std::lock_guard lock(mutex_);
        return mapped_[code] = result;
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
        auto result = query("SELECT language FROM code_map WHERE scheme='wikt-'||?1 AND code=?2 LIMIT 1", wiki, code);
        std::lock_guard lock(mutex_);
        return wikiCodes_[key] = result;
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
        const bool known = !query("SELECT code FROM language WHERE code=?2 AND ?1<>'' LIMIT 1", "x", code).empty();
        std::lock_guard lock(mutex_);
        return codes_[code] = known;
    }

private:
    sqlite3 *db_ = nullptr;
    std::map<std::string, std::string> cache_;
    std::map<std::string, bool> codes_;
    std::map<std::string, std::string> mapped_, wikiCodes_;
    fs::path file_;
    std::shared_mutex mutex_; // of the caches: shared to read them, exclusive to add

    // Names in one language (in_code), read once: exact, lower-case, of Lua modules, and the
    // whole parts of names (before or after a '-' or ' ', between two) -> code, or ambiguous
    struct NameIndex {
        std::unordered_map<std::string, std::string> exact, lower, module, part;
    };
    static constexpr const char *ambiguous = "\x01";
    std::map<std::string, std::unique_ptr<NameIndex>> indexes_; // "" : the English names of en.wikt
    std::mutex indexMutex_;

    const NameIndex &nameIndex(const std::string &lang) {
        std::lock_guard lock(indexMutex_); // one thread builds it, the others wait for it once
        auto &slot = indexes_[lang];
        if (slot)
            return *slot;
        slot = std::make_unique<NameIndex>();
        NameIndex &index = *slot;
        // The rank of a name as in ORDER BY of the former queries: lower is better
        using Rank = std::tuple<int, int, int, int, int, size_t>;
        std::unordered_map<std::string, Rank> exactRank, lowerRank;
        auto keep = [](auto &map, auto &ranks, const std::string &key, const std::string &code, const Rank &rank) {
            auto it = ranks.find(key);
            if (it == ranks.end() || rank < it->second) {
                ranks[key] = rank;
                map[key] = code;
            }
        };
        sqlite3_stmt *stmt = nullptr;
        if (lang.empty()) {
            if (sqlite3_prepare_v2(connection(), "SELECT code, name, kind FROM language WHERE name IS NOT NULL AND code NOT LIKE '%:%'",
                                   -1, &stmt, nullptr) == SQLITE_OK)
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const std::string code = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                    const std::string name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                    const auto *kind = sqlite3_column_text(stmt, 2);
                    const Rank rank{0, 0, 0, 0, !kind || std::string(reinterpret_cast<const char *>(kind)) != "language", 0};
                    keep(index.exact, exactRank, name, code, rank);
                    keep(index.lower, lowerRank, wikilib::unicode::to_lower(name), code, rank);
                }
            sqlite3_finalize(stmt);
            return index;
        }
        static const std::set<std::string> sources = {"en.wikt", "extra", "enwiki", "sil", "wikidata-label",
                                                      "wikidata-alias", "wikidata-other", "wikt-heading", "config-alias"};
        if (sqlite3_prepare_v2(connection(), "SELECT n.code, n.name, n.source, l.kind FROM language_name n "
                                             "LEFT JOIN language l ON l.code=n.code WHERE n.in_code=?1",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, lang.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const auto *c = sqlite3_column_text(stmt, 0), *n = sqlite3_column_text(stmt, 1), *src = sqlite3_column_text(stmt, 2),
                           *kind = sqlite3_column_text(stmt, 3);
                if (!c || !n)
                    continue;
                const std::string code = reinterpret_cast<const char *>(c), name = reinterpret_cast<const char *>(n);
                const std::string source = src ? reinterpret_cast<const char *>(src) : "";
                const std::string lowerName = wikilib::unicode::to_lower(name);
                // Whole parts of every name of the language (any source)
                for (size_t i = 0; i < lowerName.size(); ++i) {
                    if (lowerName[i] != '-' && lowerName[i] != ' ')
                        continue;
                    auto add = [&](const std::string &part) {
                        if (part.empty())
                            return;
                        auto [it, added] = index.part.emplace(part, code);
                        if (!added && it->second != code)
                            it->second = ambiguous;
                    };
                    add(lowerName.substr(0, i));
                    add(lowerName.substr(i + 1));
                    for (size_t j = i + 1; j < lowerName.size(); ++j)
                        if (lowerName[j] == lowerName[i])
                            add(lowerName.substr(i + 1, j - i - 1));
                }
                if (source == "wikt-module") {
                    index.module.emplace(name, code);
                    continue;
                }
                if (!kind || !sources.contains(source))
                    continue; // (a code not in language: the former JOIN)
                const Rank rank{source == "config-alias", source == "wikt-heading", source == "wikidata-other",
                                source == "wikidata-alias", std::string(reinterpret_cast<const char *>(kind)) != "language",
                                code.size()};
                keep(index.exact, exactRank, name, code, rank);
                keep(index.lower, lowerRank, lowerName, code, rank);
            }
        }
        sqlite3_finalize(stmt);
        return index;
    }

    // This thread's read-only connection: queries of the threads run side by side
    sqlite3 *connection() const {
        struct Connection {
            sqlite3 *db = nullptr;
            ~Connection() { sqlite3_close(db); }
        };
        thread_local std::map<std::string, Connection> connections;
        auto &c = connections[file_.string()];
        if (!c.db && sqlite3_open_v2(file_.string().c_str(), &c.db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
            sqlite3_close(c.db);
            c.db = nullptr;
        }
        return c.db;
    }

    std::string query(const char *sql, const std::string &a, const std::string &b) {
        sqlite3_stmt *stmt = nullptr;
        std::string result;
        if (sqlite3_prepare_v2(connection(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
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
    if (!line.starts_with('=') || line.size() > 2000 || !std::regex_match(line, m, re) || m[2].length() == 0)
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
        // nia "{{nia}}", "{{id}}": the code alone
        static const std::regex bareCode(R"(^[a-z]{2,3}(-[a-z]{2,8})*$)");
        if (config["language"].value("bare_code", false))
            return t->parameters.empty() && std::regex_match(t->name, bareCode) ? std::optional(t->name) : std::nullopt;
        auto pattern = str(config, "/language/pattern", "{{-xx-}}");
        if (pattern == "{{=xx=}}" ? std::regex_match(t->name, m, eq) : std::regex_match(t->name, m, dash))
            return m[1].str();
        return std::nullopt;
    }
    auto h = heading(line);
    if (!h || h->level != level)
        return std::nullopt;
    if (form == "heading-name") {
        // links_only: names as links only (ar "[[إنجليزية]]"), so that "== الترجمات ==" is a section
        if (config["language"].value("links_only", false) && !trim(h->text).starts_with("[["))
            return std::nullopt;
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
        // ar "[[لُغَة|لغة]] [[إِنْجْلِيزِيَّة|إنجليزية]]": several links, as their texts
        if (h->text.starts_with("[[") && h->text.ends_with("]]") && h->text.find("{{") == std::string::npos) {
            static const std::regex anyLink(R"(\[\[(?:[^\]|]*\|)?([^\]|]*)\]\])");
            return trim(std::regex_replace(h->text, anyLink, "$1"));
        }
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
        // la =={{-lingua-|la|eo}}==: a named template with the code as a parameter (not "lingua")
        if (auto name = str(config, "/language/template"); t && !name.empty() && t->name == name) {
            if (auto v = t->get(static_cast<size_t>(num(config, "/language/code", 1))); v && !v->empty())
                return std::string(*v);
            return std::nullopt;
        }
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

// Lines longer are not matched by std::regex: its recursion per character overflows the stack
constexpr size_t maxRegexLine = 20000;

// A line beginning with one of the prefixes of a list of the configuration (senses.examples)
bool linePrefixed(const json &config, const std::string &pointer, const std::string &line) {
    const json::json_pointer p(pointer);
    if (!config.contains(p))
        return false;
    for (const auto &prefix : config[p])
        if (line.starts_with(prefix.get<std::string>()))
            return true;
    return false;
}

// Sense line by the configured marker; ids are explicit or the order of '#' lines
std::optional<Sense> senseLine(const json &config, const std::string &line, std::vector<int> &counters) {
    const auto marker = str(config, "/senses/marker", "#");
    std::smatch m;
    // "line": every text line of the definitions (lo ===ຄວາມໝາຍ=== without marks)
    if (marker == "line") {
        auto text = trim(line);
        // (* lines: lists of other sections, unless "line_stars" (te: definitions as * lines too))
        static const json::json_pointer lineStars("/senses/line_stars");
        const bool stars = config.contains(lineStars) && config[lineStars].get<bool>();
        // (lines of senses.examples: examples of the definition before, lb ":*D'Jonge spillen…"; lo
        // writes definitions so)
        if (linePrefixed(config, "/senses/examples", text))
            return std::nullopt;
        if (text.empty() || text.starts_with("{{") || text.starts_with("[[Category:") ||
            (text.starts_with("*") && !stars) || text.starts_with("<") || text.starts_with("|") ||
            text.starts_with("}}"))
            return std::nullopt;
        // the headword alone in bold (te "'''ampela'''"): no definition
        static const std::regex headword(R"(^'''[^']+'''$)");
        if (text.size() < maxRegexLine && std::regex_match(text, headword))
            return std::nullopt;
        while (!text.empty() && (text[0] == ':' || text[0] == '#' || text[0] == '*')) // lo ":*text"
            text = trim(text.substr(1));
        static const std::regex number(R"(^\d+[.)]?\s+(.*)$)"); // uz "1 Biror oila…"
        if (std::smatch n; text.size() < maxRegexLine && std::regex_match(text, n, number))
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
            semicolon(R"(^;([0-9]+)\s*(.*)$)"), dotted(R"(^:\s*([0-9]+)\.\s*(.*)$)"); // nia ":1. text"
    const auto &re = marker.starts_with(": (")   ? parenthesized
                     : marker.starts_with(":[")  ? bracketed
                     : marker.starts_with(":{n}.") ? dotted
                                                   : semicolon;
    if (line.size() < maxRegexLine && std::regex_match(line, m, re))
        return Sense{m[1].str(), trim(m[2].str())};
    return std::nullopt;
}

struct Translation {
    std::string lang, term, sense;
    std::string gloss; // of the table (en {{trans-top|domestic species}})
    bool generic = false; // not by the configuration: kept only when its language is known
    std::vector<std::string> gender; // de {{Ü|de|Hund}} {{m}}, en {{t|de|Hund|m}}
    std::string tr;                  // transliteration: en {{t|ru|вода|tr=voda}}
    std::string image;               // the term as a picture: pl Egyptian-mw.PNG (hieroglyphs)
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
// (a scan, no std::regex: its recursion per character overflows the stack on long sections)
std::vector<std::string> mediaFiles(const std::string &text) {
    static const std::vector<std::string> extensions = {"ogg", "oga", "wav", "mp3", "flac", "opus", "webm", "mid",
                                                        "jpg", "jpeg", "png", "svg", "gif", "tif", "tiff", "webp"};
    static const std::string_view delimiters = "[]{}|=\n:<>";
    std::vector<std::string> result;
    for (size_t dot = text.find('.'); dot != std::string::npos; dot = text.find('.', dot + 1)) {
        // The extension: letters after the dot up to a delimiter or the end
        size_t end = dot + 1;
        while (end < text.size() && std::isalnum(static_cast<unsigned char>(text[end])))
            ++end;
        std::string extension = text.substr(dot + 1, end - dot - 1);
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (!std::ranges::contains(extensions, extension) ||
            (end < text.size() && delimiters.find(text[end]) == std::string_view::npos && text[end] != ' ' &&
             text[end] != '\t' && text[end] != '\r' && text[end] != ',' && text[end] != ')'))
            continue;
        // The name: back to a delimiter (the namespace before ':' left out)
        size_t start = dot;
        while (start > 0 && delimiters.find(text[start - 1]) == std::string_view::npos)
            --start;
        if (auto name = trim(text.substr(start, end - start)); name.size() > extension.size() + 1 &&
                                                                 !std::ranges::contains(result, name))
            result.push_back(name);
    }
    return result;
}

// The pages a translation section without translations refers to: [{"title", "gloss"}]
// (en {{trans-see|domestic animal|dog}}, {{trans-see|colour}}, tr {{çeviri yönlendirme|X}};
// else the links of a short text: nl "zie [[weer#Vertalingen]]")
json seeAlso(const std::string &text) {
    json result = json::array();
    for (const auto &t : wikilib::templates::find_invocations(text)) {
        if (t.name == "trans-see") {
            std::vector<std::string> values;
            for (size_t i = 1; auto v = t.get(i); ++i)
                values.push_back(trim(std::string(*v)));
            for (size_t i = values.size() > 1 ? 1 : 0; i < values.size(); ++i)
                if (!values[i].empty())
                    result.push_back(values.size() > 1 ? json{{"title", values[i]}, {"gloss", values[0]}}
                                                       : json{{"title", values[i]}});
        } else if (t.name == "çeviri yönlendirme" || t.name == "Çeviri yönlendirme") {
            if (auto v = t.get(size_t{1}); v && !trim(std::string(*v)).empty())
                result.push_back({{"title", trim(std::string(*v))}});
        }
    }
    // (links to a section of another page only, not unread lists of translations)
    if (static const std::regex anchored(R"(\[\[([^\]|#:]+)#[^\]|]+(?:\|[^\]]*)?\]\])");
        result.empty() && trim(text).size() < 300) {
        std::vector<std::string> targets;
        for (std::sregex_iterator it(text.begin(), text.end(), anchored), end; it != end; ++it)
            targets.push_back(trim((*it)[1].str()));
        if (!targets.empty() && targets.size() <= 3)
            for (const auto &target : targets)
                result.push_back({{"title", target}});
    }
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
    // Words of a phrase linked one by one: zh "[[посадишь]] [[сливы]] [[—]] [[сливы]]", en
    // "[[kissa]] [[i]] [[kors]]": links with only spaces and dashes between them are one term,
    // the text they show
    static const std::regex link(R"(\[\[([^\]|]+)(?:\|([^\]]*))?\]\])");
    for (const auto &piece : pieces) {
        const auto genders = lineGenders(piece);
        auto terms = links(piece);
        if (terms.size() >= 2 && piece.size() < maxRegexLine) {
            std::string shown, between;
            size_t last = 0;
            for (std::sregex_iterator it(piece.begin(), piece.end(), link), end; it != end; ++it) {
                if (last)
                    between += piece.substr(last, static_cast<size_t>(it->position()) - last);
                shown += (shown.empty() ? "" : piece.substr(last, static_cast<size_t>(it->position()) - last)) +
                         ((*it)[2].matched ? (*it)[2].str() : (*it)[1].str());
                last = static_cast<size_t>(it->position() + it->length());
            }
            if (between.find_first_not_of(" -\xE2\x80\x94") == std::string::npos) // spaces, -, — (E2 80 94)
                terms = {trim(shown)};
        }
        for (const auto &term : terms)
            result.emplace_back(term, genders);
    }
    return result;
}

// Links of files in a text ([[Plik:Egyptian-mw.PNG|18px|link=mw]]), in order: where they are, the
// file without its namespace, the target of link= (empty when none)
struct PictureLink {
    size_t at, length;
    std::string file, target;
};

std::vector<PictureLink> pictureLinkList(const std::string &text) {
    static const std::regex link(R"(\[\[[^\]|:]+:([^\]|]+\.[A-Za-z0-9]{3,4})((?:\|[^\]]*)?)\]\])");
    std::vector<PictureLink> result;
    if (text.size() >= maxRegexLine || text.find("[[") == std::string::npos)
        return result;
    for (std::sregex_iterator it(text.begin(), text.end(), link), end; it != end; ++it) {
        const auto file = trim((*it)[1].str());
        if (auto found = mediaFiles(file); found.size() != 1 || found[0] != file)
            continue;
        std::string target;
        for (const auto &option : split((*it)[2].str(), '|'))
            if (trim(option).starts_with("link="))
                target = trim(trim(option).substr(5));
        result.push_back({static_cast<size_t>(it->position(0)), static_cast<size_t>(it->length(0)), file, target});
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> pictureLinks(const std::string &text) {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto &l : pictureLinkList(text))
        result.emplace_back(l.file, l.target);
    return result;
}

// The text with its file links as links of their term: [[mw]] of link=mw, else [[the file]]
std::string withPictureLinksAsTerms(const std::string &text) {
    std::string result;
    size_t last = 0;
    for (const auto &l : pictureLinkList(text)) {
        result += text.substr(last, l.at - last) + "[[" + (l.target.empty() ? l.file : l.target) + "]]";
        last = l.at + l.length;
    }
    return result + text.substr(last);
}

// Templates of a translation section as the text they give (translations.unwrap: a template whose first
// parameter is its text, pl {{PJM-ukryj|…}}; translations.picture_templates: a term as a picture,
// pl {{PJM|woda}} -> [[File:SGN-PL SW woda.PNG|link=woda]] of the pattern "SGN-PL SW {1}.PNG")
std::string rewrittenTemplates(const std::string &text, const json &tc) {
    const auto unwrap = tc.value("unwrap", std::vector<std::string>{});
    const auto pictures = tc.value("picture_templates", json::object());
    if ((unwrap.empty() && pictures.empty()) || text.find("{{") == std::string::npos)
        return text;
    std::string result = text;
    for (bool changed = true; changed;) {
        changed = false;
        std::string next;
        size_t last = 0;
        for (const auto &t : wikilib::templates::find_invocations(result)) {
            std::string replacement;
            if (std::ranges::contains(unwrap, t.name)) {
                replacement = std::string(t.get(size_t{1}).value_or(""));
            } else if (auto it = pictures.find(t.name); it != pictures.end()) {
                const auto name = trim(std::string(t.get(size_t{1}).value_or("")));
                auto file = it->get<std::string>();
                file.replace(file.find("{1}"), 3, name);
                // "woda 2": the second sign of woda, its term woda
                static const std::regex variant(R"(\s+\d+$)");
                replacement = "[[File:" + file + "|link=" + std::regex_replace(name, variant, "") + "]]";
            } else {
                continue;
            }
            next += result.substr(last, t.location.begin.offset - last) + replacement;
            last = t.location.end.offset;
            changed = true;
        }
        if (changed)
            result = next + result.substr(last);
    }
    return result;
}

std::vector<Translation> translations(const json &config, const std::string &original, bool lines = true) {
    std::vector<Translation> result;
    const auto &tc = config.value("translations", json::object());
    const std::string text = rewrittenTemplates(original, tc);
    const auto terms = tc.value("terms", json::object());
    std::vector<std::string> names = terms.value("templates", std::vector<std::string>{});
    const int langParam = terms.value("lang", 1);
    const auto termParam = terms.contains("term") ? terms["term"] : json(2);
    const auto senseParam = terms.value("sense", std::string());
    const auto table = tc.value("table", json());
    std::string tableSense; // number of the current table (fr, de)
    std::string tableGloss; // gloss of the current table (en, sv)
    // kbd: a language template on its own line ({{инджылыбзэ}}), then "*унэ: 1. house, dwelling 2. family":
    // the first line of the language is the entry (the next ones are phrases), its text the translations
    // (plain words, "N." the sense numbers, notes in parentheses left out)
    if (terms.value("form", std::string()) == "language-lines") {
        static const std::regex number(R"(^\s*([0-9]+)\.\s*)");
        std::string lang;
        bool taken = true;
        std::istringstream in(text);
        for (std::string l; std::getline(in, l);) {
            l = trim(l);
            if (auto t = templateLine(l); t && t->parameters.empty()) {
                lang = t->name;
                taken = false;
                continue;
            }
            const auto colon = l.find(':');
            if (taken || lang.empty() || !l.starts_with("*") || colon == std::string::npos)
                continue;
            taken = true;
            std::string rest = l.substr(colon + 1);
            for (size_t open; (open = rest.find('(')) != std::string::npos;)
                rest.erase(open, rest.find(')', open) == std::string::npos ? std::string::npos
                                                                            : rest.find(')', open) - open + 1);
            // "1. house, dwelling 2. family": the pieces between numbers
            std::vector<std::pair<std::string, std::string>> parts; // sense, text
            static const std::regex inner(R"((?:^|\s)([0-9]+)\.\s)");
            std::string sense;
            size_t from = 0;
            for (std::sregex_iterator it(rest.begin(), rest.end(), inner), end; it != end; ++it) {
                parts.emplace_back(sense, rest.substr(from, static_cast<size_t>(it->position(0)) - from));
                sense = (*it)[1].str();
                from = static_cast<size_t>(it->position(0) + it->length(0));
            }
            parts.emplace_back(sense, rest.substr(from));
            for (const auto &[s, piece] : parts)
                for (const auto &word : split(piece, ',')) // also ";"
                    for (const auto &w : split(word, ';')) {
                        auto term = trim(w);
                        while (!term.empty() && std::string_view(".:").find(term.back()) != std::string_view::npos)
                            term = trim(term.substr(0, term.size() - 1));
                        // "see "…"", "см. …": references, not translations
                        if (term.empty() || term.find('"') != std::string::npos || term.find("{{") != std::string::npos ||
                            term.starts_with("см.") || term.starts_with("see "))
                            continue;
                        result.push_back({lang, term, s, ""});
                    }
        }
        return result;
    }
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
        // An empty code (sv "*herero: {{ö||omeva}}"): the language the line begins with
        std::string lineLanguage;
        if (trim(std::string(*lang)).empty() && t.location.begin.offset < text.size()) {
            const size_t start = text.rfind('\n', t.location.begin.offset);
            const auto head = text.substr(start == std::string::npos ? 0 : start + 1,
                                          t.location.begin.offset - (start == std::string::npos ? 0 : start + 1));
            static const std::regex named(R"(^[:*#]*\s*([^:{}\[\]]+?)\s*:\s*$)");
            if (std::smatch m; head.size() < 200 && std::regex_match(head, m, named))
                lineLanguage = m[1].str();
            lang = lineLanguage;
        }
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
            if (l.size() >= maxRegexLine || !std::regex_match(l, m, line))
                continue;
            std::string lang = m[2].matched ? m[2].str() : trim(m[1].str());
            // nia "* {{-en-}}: [[red]]"
            if (m[2].matched && lang.size() > 2 && lang.front() == '-' && lang.back() == '-')
                lang = lang.substr(1, lang.size() - 2);
            // bn "* [[ইংরেজি]]: [[dog]]": the name is a link
            if (lang.starts_with("[[") && lang.ends_with("]]")) {
                lang = lang.substr(2, lang.size() - 4);
                if (auto bar = lang.find('|'); bar != std::string::npos)
                    lang = lang.substr(bar + 1);
            }
            // pl "(1.1) [[амэрыканка]] {{ż}}; (1.2) [[амэрыканка]] {{ż}}": a number further on the line
            // is the sense of the terms after it
            static const std::regex senseMark(R"(\(([0-9]{1,2}(?:\.[0-9]{1,2})*(?:\s*[,-]\s*[0-9]{1,2}(?:\.[0-9]{1,2})*)*)\))");
            const std::string rest = m[4].str();
            std::string sense = m[3].matched && m[3].length() ? m[3].str() : lineSense;
            size_t from = 0;
            auto take = [&](std::string piece) {
                // A term as a picture (pl hieroglyphs "[[Plik:Egyptian-mw.PNG|18px|link=mw]]", signs of a
                // sign language "[[Plik:Sign language A.svg|50px]]"): the term of its link= (else the file),
                // the file its image
                std::map<std::string, std::vector<std::string>> images; // term -> files, in order
                for (const auto &[file, target] : pictureLinks(piece))
                    images[target.empty() ? file : target].push_back(file);
                piece = withPictureLinksAsTerms(piece);
                for (const auto &[term, genders] : linkPieces(piece)) {
                    Translation item{lang, term, sense, lineGloss};
                    item.gender = genders;
                    if (auto it = images.find(term); it != images.end() && !it->second.empty()) {
                        item.image = it->second.front();
                        it->second.erase(it->second.begin());
                    }
                    result.push_back(std::move(item));
                }
            };
            if (m[3].matched && m[3].length() && rest.size() < maxRegexLine)
                for (std::sregex_iterator it(rest.begin(), rest.end(), senseMark), end; it != end; ++it) {
                    take(rest.substr(from, static_cast<size_t>(it->position(0)) - from));
                    sense = (*it)[1].str();
                    from = static_cast<size_t>(it->position(0) + it->length(0));
                }
            take(rest.substr(from));
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
                if (l.size() >= maxRegexLine || !std::regex_match(l, m, line))
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
        "romanization", "part of speech" /* kk ===Сөз табы===: the part of speech as a line, the senses */,
        "expression", "determinative" /* en ===Phrase===, ===Determiner=== by their Wikidata labels */};
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
        std::string lang = wiki;
        std::ranges::replace(lang, '_', '-');
        // the language of the Wikidata labels of the wiki: no.wikt is in Bokmål (nb), as language names
        static const std::map<std::string, std::string> labelLanguage = {
                {"no", "nb"}, {"simple", "en"}, {"zh-min-nan", "nan"}, {"roa-rup", "rup"}, {"bh", "bho"}};
        if (auto it = labelLanguage.find(lang); it != labelLanguage.end())
            lang = it->second;
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
        // Kinds of the table: the parts of speech and lexical relations of Wikidata (wiktsections
        // --pos, --relations: "Relative pronoun", "Negation", "Holonyms")
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT section, kind FROM section WHERE kind IN ('pos','relation')", -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW)
            kinds_[wikilib::unicode::to_lower(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)))] =
                    reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        sqlite3_finalize(stmt);
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
                    {"subst", "noun"}, {"sb", "noun"}, {"vb", "verb"}, {"npr", "proper noun"},
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
        if (auto k = kinds_.find(lower); kind == "section" && k != kinds_.end())
            kind = k->second;
        return SectionName{section, kind};
    }

private:
    std::map<std::string, std::string> names_; // first source wins
    std::map<std::string, std::string> kinds_; // lowercase section -> pos, relation (wiktsections.sqlite)
};

// Key of a heading or template line for SectionNames: the text, a template's
// name or its first parameter (fr {{S|nom|fr}}, de {{Wortart|Substantiv|Deutsch}}),
// without {{-…-}} dashes (it {{-sin-}}, nl {{-noun-|nld}}) and numbering ("Etymology 2")
std::pair<std::string, int> sectionKey(std::string text) {
    // "Etymology 2", zh "詞源2" (no space), lv "Izruna Nr.2"
    static const std::regex numbered(R"(^(.*[^\d\s.])\s*(?:Nr\.|№)?\s*(\d+)$)");
    text = trim(text);
    if (text.starts_with("{{"))
        if (auto all = wikilib::templates::find_invocations(text); !all.empty())
            text = text.substr(0, all.front().location.length()); // de "{{Wortart|Substantiv|Deutsch}}, {{n}}"
    if (auto t = onlyTemplate(trim(text))) {
        // wa {{H|sustantif|wa}}, vi {{ĐM|pron}}
        // (tk {{Söztopary|Idioma|Türkmençe}})
        static const std::set<std::string> byParameter = {"S", "Wortart", "-pos-", "H", "ĐM", "Söztopary"};
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
    // bn "ব্যুৎপত্তি ১", hi "व्युत्पत्ति २": digits of other scripts (U+09E6, U+0966 and on) as ASCII
    for (const char *zero : {"০", "०"})
        for (int d = 0; d < 10; ++d) {
            std::string digit = zero;
            digit[2] = static_cast<char>(digit[2] + d);
            for (size_t at; (at = text.find(digit)) != std::string::npos;)
                text.replace(at, digit.size(), std::string(1, static_cast<char>('0' + d)));
        }
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
        // lb "{{Substantiv|lb}}{{n}} {{k M|lb}}": a part of speech as the template a line begins with
        // (pos.leading_template); a level below the language, so that the template lines after it
        // ({{Bedeitung}}) are its sections; the rest of the line its text
        if (config_.value("pos", json::object()).value("leading_template", false) && line.starts_with("{{"))
            if (auto all = wikilib::templates::find_invocations(line);
                !all.empty() && all.front().location.begin.offset == 0)
                if (auto name = names_.find(sectionKey(line.substr(0, all.front().location.length())).first, 2);
                    name && name->kind == "pos") {
                    open(line.substr(0, all.front().location.length()), root_["level"].get<int>() + 1, false);
                    current()["text"] = trim(line.substr(all.front().location.length())) + '\n';
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
            // A headword template under its part of speech (en ===Verb=== {{en-verb}}) is a line of
            // it, not a part of speech of its own (shn {{shn-noun}} without a heading is one)
            const bool headword = dashTemplate(line) == 2 && !names_.find(sectionKey(trim(line)).first) &&
                                  current()["kind"] == "pos";
            if (!headword) {
                open(trim(line), headingLevel() + 1, true); // {{synonimy}}, {{-sin-}}
                return;
            }
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
        // Senses right under the language heading, before any section (ar "== {{اللغة|عربية}} ==" then
        // "# …"): in a Definitions section of their own, so that they are where all senses are
        // (not of the "line" marker, for which every line of text would be a sense)
        // (and not of wikis whose "#" lines there are no definitions: senses.implicit false, gn pronunciation)
        static const json::json_pointer implicitSenses("/senses/implicit");
        // (skr "* [[آب]]۔": of its marker)
        const auto marker = str(config_, "/senses/marker", "#");
        if (stack_.size() == 1 && (line.starts_with('#') || (marker == "*" && line.starts_with('*'))) && marker != "line" &&
            !(config_.contains(implicitSenses) && !config_[implicitSenses].get<bool>()))
            if (std::vector<int> probe; senseLine(config_, line, probe)) {
                push(node(std::optional<std::string>("Definitions"), "section", "", headingLevel() + 1, 0));
                current()["implicit"] = true;
                counters_.clear();
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
        if (auto &senses = current()["senses"];
            !senses.empty() && (line.starts_with('#') ||
                                (str(config_, "/senses/marker") == "line" && linePrefixed(config_, "/senses/examples", line)))) {
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
        // ru == {{з|ударение=за́мок}} ==: a homograph, its sections below
        if (!name && !key.empty() && key == str(config_, "/homographs/template"))
            name = SectionName{"Homograph", "section"};
        if (!name)
            unknown_.push_back(key);
        std::string kind = !name ? "" : name->section == "Etymology" ? "etymology" : name->kind;
        json n = node(name ? std::optional(name->section) : std::nullopt, kind, raw, level, index);
        if (templateLine)
            n["template"] = true;
        push(std::move(n));
        counters_.clear();
    }

    // A part of speech of a text: without list marks, links, bold, a remark in brackets
    // (gu "પ્રત્યય (સં)"); the whole or its first word, of kind pos but the generic "Part of speech"
    // (sat "<nowiki>*</nowiki>ᱜᱩᱱᱩᱱ", "᱑ᱹ ᱧᱩᱱᱩᱢ" numbered in Ol Chiki digits, "* ᱧᱩᱱᱩᱢ।" with a danda,
    // "(ᱧᱩᱱᱩᱢ)", two of them "ᱠᱟᱱᱣᱟ, ᱜᱩᱱᱩᱢ": the first one known)
    std::optional<std::string> posOf(std::string text) const {
        static const std::regex link(R"(\[\[(?:[^\]|]*\|)?([^\]|]*)\]\])"), tags("<[^>]*>|''+"),
                wrapped(R"(^\s*\(([^()]*)\)\s*$)"), remark(R"(\([^)]*\))"),
                number("^\\s*(?:[0-9]|\xE1\xB1[\x90-\x99])+(?:[.)]|\xE1\xB1\xB9)?\\s*");
        if (text.size() > 200)
            return std::nullopt;
        text = std::regex_replace(std::regex_replace(text, link, "$1"), tags, "");
        while (!text.empty() && std::string_view("*#: ").find(text.front()) != std::string_view::npos)
            text.erase(0, 1);
        text = std::regex_replace(std::regex_replace(std::regex_replace(text, wrapped, "$1"), remark, ""), number, "");
        for (std::string_view end : {".", ",", ":", ";", " ", "\xE0\xA5\xA4" /* । */, "\xE1\xB1\xBE" /* ᱾ */})
            while (text.ends_with(end))
                text.erase(text.size() - end.size());
        text = trim(text);
        std::vector<std::string> candidates = {text, text.substr(0, text.find(' '))};
        for (const auto &piece : split(text, ','))
            for (const auto &part : split(piece, '/'))
                candidates.push_back(trim(part));
        for (const auto &candidate : candidates)
            if (auto name = names_.find(trim(candidate)); name && name->kind == "pos" && name->section != "Part of speech")
                return name->section;
        return std::nullopt;
    }

    // Under a heading of no particular part of speech ("Part of speech": ne पद वर्ग, lo ປະເພດ, ig
    // Nkejiasụsụ) the part of speech is a parameter of the heading (ka
    // {{კატეგორია|ქართული|არსებითი სახელი}}), its first line (ne "नाम"; lo "ຄຳນາມ", taken as the
    // first sense of the "line" marker, which is no sense then), or a headword template (om {{Om-noun}})
    void resolvePartOfSpeech(json &n) const {
        std::optional<std::string> found;
        if (auto t = onlyTemplate(n.value("heading", std::string())))
            for (size_t i = 2; i >= 1 && !found; --i)
                if (auto v = t->get(i))
                    found = posOf(std::string(*v));
        if (!found) {
            // its first line of text (not a file [[चित्र:…]], a link of a namespace; not a template)
            for (const auto &line : split(n.value("text", std::string()), '\n'))
                if (auto first = trim(line);
                    !first.empty() && !(first.starts_with("[[") && first.find(':') < first.find("]]"))) {
                    if (!first.starts_with("{{"))
                        found = posOf(first);
                    break;
                }
        }
        if (!found && !n["senses"].empty())
            if ((found = posOf(n["senses"][0]["text"].get<std::string>()))) {
                n["text"] = n["senses"][0]["text"].get<std::string>() + '\n' + n.value("text", std::string());
                n["senses"].erase(0);
                // ids of the order of lines: renumbered
                bool ordered = true;
                for (size_t i = 0; i < n["senses"].size(); ++i)
                    ordered &= n["senses"][i]["id"] == std::to_string(i + 2);
                if (ordered)
                    for (size_t i = 0; i < n["senses"].size(); ++i)
                        n["senses"][i]["id"] = std::to_string(i + 1);
            }
        if (!found)
            for (const auto &t : wikilib::templates::find_invocations(n.value("text", std::string())))
                if (auto name = names_.find(t.name, 2); name && name->kind == "pos" && name->section != "Part of speech") {
                    found = name->section;
                    break;
                }
        if (found)
            n["section"] = *found;
    }

    void finishNode(json &n, Stats &stats) {
        if (n["section"] == "Part of speech")
            resolvePartOfSpeech(n);
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
            if (!t.image.empty())
                item["image"] = t.image;
            if (code != t.lang)
                item["lang_written"] = t.lang; // pl "angielski", it "{{en}}" -> en
            n["translations"].push_back(std::move(item));
        }
        // ar {{معجمية|L1480479|معاني}}: the senses are those of a Wikidata lexeme, not in the wikitext
        if (auto lexemeTemplate = str(config_, "/senses/lexeme_template"); !lexemeTemplate.empty())
            for (const auto &t : allTemplates(n.value("text", std::string())))
                if (t.name == lexemeTemplate)
                    if (auto id = t.get(size_t{1}); id && id->size() > 1 && (*id)[0] == 'L') {
                        n["lexeme"] = trim(std::string(*id));
                        break;
                    }
        stats.senses += n["senses"].size();
        stats.translations += n["translations"].size();
        n.erase("template_pos");
        // The text of sections handled in this phase is not repeated: "source" points into the dump
        // Files of Wikimedia Commons: sounds of the pronunciation, pictures ([[Plik:Pl-woda.ogg]],
        // {{audio|De-rot.ogg}}), without the namespace, which every wiki names in its language
        if (auto files = mediaFiles(n.value("text", std::string())); !files.empty())
            n["media"] = files;
        // Translations on another page: en {{trans-see|gloss|color}}, tr {{çeviri yönlendirme|X}},
        // nl "zie [[weer#Vertalingen]]" (en {{see translation subpage}}: wiktlink merges the subpage)
        if (n["section"] == "Translations" && n["translations"].empty())
            if (auto see = seeAlso(n.value("text", std::string())); !see.empty())
                n["see"] = see;
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
                !c.contains("children") && !c.contains("see") && c.value("kind", json()) != "pos")
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

// ---------------------------------------------------------------------------
// The uniform tree (--uniform; doc/uniform-tree.md, res/schema/uniform.schema.json): the tree of
// a language's headings as entries (one per part of speech, group and form) with their senses,
// translations and relations; what stands above the parts of speech goes to the entries it
// belongs to: by the headings, by the sense ids of its lines, else to the language.
namespace uniform {

// A language as written (a code, a name of the wiki) as its canonical code; empty when not known
using LanguageCode = std::function<std::string(const std::string &)>;

std::vector<std::string> textLines(const std::string &text) {
    std::vector<std::string> result;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
        result.push_back(line);
    return result;
}

// "1.1, 1.5-7" -> 1.1 1.5 1.6 1.7; de "1–3" -> 1 2 3
std::vector<std::string> expandIds(std::string written) {
    for (size_t at; (at = written.find("\xE2\x80\x93")) != std::string::npos;) // – as -
        written.replace(at, 3, "-");
    std::vector<std::string> ids;
    for (const auto &raw : split(written, ',')) {
        const auto part = trim(raw);
        const auto dash = part.find('-');
        if (dash == std::string::npos) {
            if (!part.empty())
                ids.push_back(part);
            continue;
        }
        const auto left = trim(part.substr(0, dash)), right = trim(part.substr(dash + 1));
        const auto dot = left.rfind('.');
        const auto prefix = dot == std::string::npos ? std::string() : left.substr(0, dot + 1);
        const auto rightDot = right.rfind('.');
        try {
            const int from = std::stoi(left.substr(prefix.size()));
            const int to = std::stoi(rightDot == std::string::npos ? right : right.substr(rightDot + 1));
            if (to < from || to - from > 50)
                throw std::invalid_argument(part);
            for (int i = from; i <= to; ++i)
                ids.push_back(prefix + std::to_string(i));
        } catch (const std::exception &) {
            ids.push_back(part);
        }
    }
    return ids;
}

// Sense ids at the start of a line: pl ": (1.1, 1.5-7) …", de ":[1, 6] …"
struct Mark {
    std::string written; // "1.1, 1.5-7"
    std::vector<std::string> ids;
    std::string rest; // the line after the mark
};

std::optional<Mark> lineMark(const std::string &line) {
    static const std::regex mark("^[:*#]*\\s*[(\\[]\\s*([0-9][0-9a-z.,\\s\\-\xE2\x80\x93]*)[)\\]]\\s*");
    const auto head = line.substr(0, 120);
    std::smatch m;
    if (!std::regex_search(head, m, mark))
        return std::nullopt;
    auto written = trim(m[1].str());
    return Mark{written, expandIds(written), line.substr(static_cast<size_t>(m.length(0)))};
}

std::string withoutListMarks(const std::string &line) {
    size_t start = 0;
    while (start < line.size() && (line[start] == ':' || line[start] == '*' || line[start] == '#'))
        ++start;
    return trim(line.substr(start));
}

void append(json &object, const std::string &key, const std::string &text) {
    if (trim(text).empty())
        return;
    object[key] = object.contains(key) ? object[key].get<std::string>() + "\n" + text : text;
}

// A list value of a template without inline modifiers (en "bar<id:shoal>", "x<q:archaic>")
std::string termValue(std::string value) {
    if (auto lt = value.find('<'); lt != std::string::npos)
        value = value.substr(0, lt);
    if (value.find("[[") != std::string::npos)
        if (auto l = links(value); !l.empty())
            return l.front();
    return trim(value);
}

bool noTerm(const std::string &term) {
    return term.empty() || term == "?" || term == "-" || term == "\xE2\x80\x94" || term == "\xE2\x80\x93";
}

// The terms of a line of a relation section with the label before them:
// "[[warownia]], [[twierdza]]", "{{zdrobn}} [[zameczek]] {{m}}", en "{{col|en|a|b}}", "* {{l|en|x}}",
// fr "{{lien|x|fr}}", ru "{{родств-блок|прилагательные=за́мковый}}" (by its parameters as labels)
std::vector<std::pair<std::string, std::string>> lineTerms(std::string rest) {
    static const std::set<std::string> linkTemplates = {"l", "link", "l-self", "ll", "m", "L", "lien"};
    static const std::regex listTemplate(R"(^(col[1-9u]?|col-auto|(der|rel|desc)[2-5]?(-u)?)$)");
    std::vector<std::pair<std::string, std::string>> terms; // label, term
    std::string label;
    // labels: templates of no terms before the first term (pl {{zdrobn}}, ru {{част.}}:)
    while (rest.starts_with("{{")) {
        auto all = wikilib::templates::find_invocations(rest);
        if (all.empty() || all.front().location.begin.offset != 0)
            break;
        const auto &t = all.front();
        if (linkTemplates.contains(t.name) || std::regex_match(t.name, listTemplate) || t.name.starts_with("родств"))
            break;
        label += (label.empty() ? "" : " ") + rest.substr(0, t.location.length());
        rest = rest.substr(t.location.length());
        while (!rest.empty() && (rest[0] == ' ' || rest[0] == ':'))
            rest.erase(0, 1);
    }
    for (const auto &[term, genders] : linkPieces(rest))
        terms.emplace_back(label, term);
    for (const auto &t : wikilib::templates::find_invocations(rest)) {
        if (linkTemplates.contains(t.name)) {
            if (auto v = t.get(size_t{t.name == "lien" ? 1u : 2u}))
                terms.emplace_back(label, termValue(std::string(*v)));
        } else if (std::regex_match(t.name, listTemplate)) {
            for (size_t i = 2; auto v = t.get(i); ++i)
                terms.emplace_back(label, termValue(std::string(*v)));
        } else if (t.name.starts_with("родств")) {
            for (const auto &[name, value] : t.parameters)
                if (!name.empty() && !std::isdigit(static_cast<unsigned char>(name[0])) && name != "полн")
                    for (const auto &piece : split(value, ','))
                        terms.emplace_back(name, termValue(piece));
        }
    }
    std::erase_if(terms, [](const auto &p) { return noTerm(p.second); });
    return terms;
}

// Items of a relation section: {term, sense, qualifier}; the sense of a mark, or of ru "#" lines
// by their place (the n-th line of sense n)
std::vector<json> relationItems(const std::string &text) {
    std::vector<json> items;
    int place = 0;
    for (const auto &line : textLines(text)) {
        const auto t = trim(line);
        if (t.empty())
            continue;
        std::string sense;
        if (t.starts_with('#') && !t.starts_with("#:") && !t.starts_with("#*"))
            sense = std::to_string(++place);
        std::string rest = withoutListMarks(t);
        if (auto mark = lineMark(t)) {
            sense = mark->written;
            rest = trim(mark->rest);
        }
        for (const auto &[label, term] : lineTerms(rest)) {
            json item = {{"term", term}};
            if (!sense.empty())
                item["sense"] = sense;
            if (!label.empty())
                item["qualifier"] = label;
            items.push_back(std::move(item));
        }
    }
    return items;
}

// Relations of sense lines: en "#: {{syn|en|banker|banque}}"
const std::map<std::string, std::string> &senseRelationTemplates() {
    static const std::map<std::string, std::string> names = {
            {"syn", "Synonyms"},         {"synonyms", "Synonyms"},     {"ant", "Antonyms"},
            {"antonyms", "Antonyms"},    {"hyper", "Hypernyms"},       {"hypernyms", "Hypernyms"},
            {"hypo", "Hyponyms"},        {"hyponyms", "Hyponyms"},     {"cot", "Coordinate terms"},
            {"coord", "Coordinate terms"}, {"holo", "Holonyms"},       {"holonyms", "Holonyms"},
            {"mero", "Meronyms"},        {"meronyms", "Meronyms"},     {"tropo", "Troponyms"}};
    return names;
}

// Files with their caption and its sense: [[Plik:x.jpg|thumb|zamek (1.1)]], ru {{илл|x.jpg|Замок [1]}};
// other files of the text by name only
json mediaItems(const std::string &text, const json &names = json::array()) {
    json result = json::array();
    std::set<std::string> seen;
    auto add = [&](const std::string &file, const std::string &caption) {
        if (file.empty() || seen.contains(file))
            return;
        seen.insert(file);
        json item = {{"file", file}};
        if (!caption.empty()) {
            item["caption"] = caption;
            static const std::regex senseOf("[(\\[]\\s*([0-9][0-9.,\\s\\-]*)[)\\]]");
            if (std::smatch m; caption.size() < maxRegexLine && std::regex_search(caption, m, senseOf))
                item["sense"] = trim(m[1].str());
        }
        result.push_back(std::move(item));
    };
    static const std::set<std::string> options = {"thumb", "thumbnail", "mini", "miniatur", "right", "left", "center",
                                                  "none", "upright", "frame", "frameless", "border", "vignette",
                                                  "redresse", "droite", "gauche", "centre"};
    for (size_t open = text.find("[["); open != std::string::npos; open = text.find("[[", open + 2)) {
        // the link up to its "]]", links in its caption inside
        int depth = 0;
        size_t end = open;
        for (; end + 1 < text.size(); ++end) {
            if (text[end] == '[' && text[end + 1] == '[')
                ++depth, ++end;
            else if (text[end] == ']' && text[end + 1] == ']' && --depth == 0)
                break;
            else if (text[end] == ']' && text[end + 1] == ']')
                ++end;
        }
        if (depth != 0)
            break;
        const auto inner = text.substr(open + 2, end - open - 2);
        std::vector<std::string> parts;
        int nested = 0;
        parts.emplace_back();
        for (size_t i = 0; i < inner.size(); ++i) {
            if (i + 1 < inner.size() && (inner[i] == '[' || inner[i] == '{') && inner[i + 1] == inner[i])
                ++nested;
            else if (i + 1 < inner.size() && (inner[i] == ']' || inner[i] == '}') && inner[i + 1] == inner[i])
                --nested;
            if (inner[i] == '|' && nested == 0)
                parts.emplace_back();
            else
                parts.back() += inner[i];
        }
        const auto colon = parts[0].find(':');
        if (colon == std::string::npos)
            continue;
        const auto file = trim(parts[0].substr(colon + 1));
        if (auto found = mediaFiles(file); found.size() != 1 || found[0] != file)
            continue;
        std::string caption;
        if (parts.size() > 1) {
            const auto last = trim(parts.back());
            if (!options.contains(wikilib::unicode::to_lower(last)) && !last.ends_with("px") &&
                last.find('=') == std::string::npos)
                caption = last;
        }
        add(file, caption);
        open = end;
    }
    for (const auto &t : allTemplates(text))
        if (auto v = t.get(size_t{1})) {
            const auto file = trim(std::string(*v));
            if (auto found = mediaFiles(file); found.size() == 1 && found[0] == file) {
                auto caption = t.get(size_t{2});
                add(file, caption && mediaFiles(trim(std::string(*caption))).empty() ? trim(std::string(*caption)) : "");
            }
        }
    for (const auto &file : mediaFiles(text))
        add(file, "");
    for (const auto &file : names)
        add(file.get<std::string>(), "");
    return result;
}

// The etymology as a text a program reads, one item a line: "n. relation language form", " [m]" when
// it is of item m, else of the entry (doc/uniform-tree.md): "1. from enm water\n2. from ang wæter\n…".
// By the configuration of the wiki (etymology.templates: a template's relation and the parameters of
// its language and form, or of its parts; etymology.words: the words before a mention, {{m}}, that
// give its relation, "from", "cognate with"). An item in brackets of the text is of the item before
// them; a descendant of the item before it. After the first paragraph with items of the chain (from,
// borrowed, calque, compound) only cognates and descendants: the rest are notes ("The plural housen
// is from…").
// entryCode: of the language of the entry (templates of "lang": "entry", pl {{odczasownikowy od|boksować}})
std::string etymologyChain(const std::string &text, const json &config, const std::string &entryCode,
                           const LanguageCode &languageCode) {
    const auto spec = config.value("etymology", json::object());
    const auto templates = spec.value("templates", json::object());
    if (text.empty() || templates.empty())
        return {};
    const auto words = spec.value("words", json::object());
    const auto markers = spec.value("marker_templates", json::object()); // pl {{por}}: as the words "compare"
    // A language as written as its code: an abbreviation of the wiki's (pl "niem", language_names, before
    // codes: pl "ang" is English), a code, a name; else as written (spaces as _)
    const auto languageNames = spec.value("language_names", json::object());
    auto resolve = [&](const std::string &written) -> std::string {
        if (auto it = languageNames.find(written); it != languageNames.end())
            return it->get<std::string>();
        if (auto code = languageCode(written); !code.empty())
            return code;
        std::string w = written;
        std::ranges::replace(w, ' ', '_');
        return w;
    };
    static const std::set<std::string> chainRelations = {"from", "borrowed", "calque", "compound", "root"};
    static const std::regex refs(R"(<ref[^>/]*/>|<ref[^>]*>[\s\S]*?</ref>)");
    const std::string clean = text.size() < maxRegexLine ? std::regex_replace(text, refs, " ") : text;
    std::vector<std::string> items;
    bool chainDone = false;
    // paragraphs: blank lines between them
    std::vector<std::string> paragraphs(1);
    for (const auto &line : textLines(clean)) {
        if (trim(line).empty()) {
            if (!paragraphs.back().empty())
                paragraphs.emplace_back();
        } else
            paragraphs.back() += line + '\n';
    }
    for (const auto &paragraph : paragraphs) {
        bool chainHere = false;
        std::string relationOfWords; // of the last words before a mention, in this sentence
        std::vector<int> anchors;    // the item a bracket of the text is of
        int last = -1;               // the last item
        size_t at = 0;
        auto read = [&](std::string_view between) {
            const auto lower = wikilib::unicode::to_lower(std::string(between));
            // the words that end last, of them the longest ("borrowed from", not "from")
            size_t best = std::string::npos, bestEnd = 0, bestSize = 0;
            for (const auto &[word, relation] : words.items())
                if (auto found = lower.rfind(word); found != std::string::npos &&
                                                    (found + word.size() > bestEnd ||
                                                     (found + word.size() == bestEnd && word.size() > bestSize))) {
                    best = found;
                    bestEnd = found + word.size();
                    bestSize = word.size();
                    relationOfWords = relation.get<std::string>();
                }
            for (size_t i = 0; i < between.size(); ++i) {
                if (between[i] == '(')
                    anchors.push_back(last);
                else if (between[i] == ')' && !anchors.empty())
                    anchors.pop_back();
                else if (between[i] == '.' && (i + 1 == between.size() || between[i + 1] == ' ' || between[i + 1] == '\n') &&
                         (best == std::string::npos || i > best))
                    relationOfWords.clear(); // the end of a sentence
            }
        };
        std::string lastRelation;
        int lastOf = -1;
        // (summary: of a chain template, which closes no chain, the written one may follow)
        auto add = [&](std::string relation, const std::string &lang, const std::string &form, int of,
                       bool summary = false) {
            std::string line = relation + " " + lang + " " + form;
            if (of >= 0)
                line += " [" + std::to_string(of + 1) + "]";
            // (once: en {{etymon}} and the chain written after it)
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].substr(items[i].find(' ') + 1) == line)
                    return;
            items.push_back(std::to_string(items.size() + 1) + ". " + line);
            last = static_cast<int>(items.size()) - 1;
            lastRelation = relation;
            lastOf = of;
            chainHere |= !summary && chainRelations.contains(relation) && relation != "root"; // (en {{root}} stands above)
        };
        for (const auto &t : wikilib::templates::find_invocations(paragraph)) {
            const auto between = std::string_view(paragraph).substr(at, t.location.begin.offset - at);
            read(between);
            at = t.location.end.offset;
            // A template of the chain as pairs of a relation and items (etymology.chain_template: en
            // {{etymon|en|:inh|enm:red<id:color>|…}}, ":inh" by its relations, "enm:red" language:form)
            if (const auto chainTemplate = spec.value("chain_template", json::object());
                !chainDone && std::ranges::contains(chainTemplate.value("names", std::vector<std::string>{}), t.name)) {
                const auto keys = chainTemplate.value("relations", json::object());
                std::string relation;
                std::vector<std::string> parts;
                std::string partsLang;
                auto flush = [&] {
                    std::string form;
                    for (const auto &part : parts)
                        form += (form.empty() ? "" : " + ") + part;
                    if (!parts.empty())
                        add("compound", partsLang, form, -1, true);
                    parts.clear();
                };
                for (size_t i = 2; auto v = t.get(i); ++i) {
                    const auto value = trim(std::string(*v));
                    if (value.starts_with(':')) {
                        flush();
                        auto k = keys.find(value.substr(1));
                        relation = k == keys.end() ? "" : k->get<std::string>();
                    } else if (auto colon = value.find(':'); colon != std::string::npos && !relation.empty()) {
                        const auto lang = value.substr(0, colon), form = termValue(value.substr(colon + 1));
                        if (noTerm(form))
                            continue;
                        if (relation == "compound") {
                            partsLang = partsLang.empty() ? resolve(lang) : partsLang;
                            parts.push_back(form);
                        } else
                            add(relation, resolve(lang), form, -1, true);
                        // the etymology of the item in it, ro "fr:dogue<ety:uder<en:dog>>": dogue from en dog
                        static const std::regex nested(R"(<ety:([^<>:]+)<([^<>:]+):([^<>]+))");
                        std::string rest = value.substr(colon + 1);
                        for (std::smatch m; rest.size() < 2000 && std::regex_search(rest, m, nested);
                             rest = m.suffix().str()) {
                            auto k = keys.find(m[1].str());
                            if (k != keys.end() && k->get<std::string>() != "compound")
                                add(k->get<std::string>(), resolve(m[2].str()), termValue(m[3].str()), -1, true);
                        }
                    }
                }
                flush();
                continue;
            }
            if (auto m = markers.find(t.name); m != markers.end()) {
                relationOfWords = m->get<std::string>();
                continue;
            }
            auto it = templates.find(t.name);
            if (it == templates.end())
                continue;
            const auto &s = *it;
            std::string relation = s.value("relation", std::string("mention"));
            // a mention right after an item, by a comma only ({{cog|nds|root}}, {{m|nds|rood}}): another
            // form of it
            const bool sameItem = relation == "mention" && last >= 0 && trim(std::string(between)) == ",";
            if (sameItem)
                relation = lastRelation;
            else if (relation == "mention" && !relationOfWords.empty())
                relation = relationOfWords;
            // (after the paragraph of the chain only its cognates and descendants: the rest are notes)
            if (chainDone && relation != "compare" && relation != "descendant")
                continue;
            auto param = [&](const char *key) -> std::string {
                if (!s.contains(key) || !s[key].is_number_integer())
                    return {};
                auto v = t.get(static_cast<size_t>(s[key].get<int>()));
                return v ? trim(std::string(*v)) : std::string();
            };
            // "lang": "entry", the language of the entry (pl {{odczasownikowy od|boksować}})
            const std::string lang = s.value("lang", json()) == "entry" ? entryCode : resolve(param("lang"));
            std::string form;
            if (s.contains("parts")) {
                // {{af|en|water|fall}}, {{suffix|en|water|y}} -> water + -y
                std::vector<std::string> parts;
                for (size_t i = s["parts"].get<size_t>(); auto v = t.get(i); ++i)
                    if (auto part = termValue(std::string(*v)); !noTerm(part))
                        parts.push_back(part);
                const auto affix = s.value("affix", std::string());
                if (affix == "suffix" && !parts.empty() && !parts.back().starts_with('-'))
                    parts.back() = "-" + parts.back();
                if (affix == "prefix" && !parts.empty() && !parts.front().ends_with('-'))
                    parts.front() += "-";
                // one part: of the relation "single" (pl {{etymn|białor|хлопец}}: from хлопец)
                if (parts.size() == 1 && s.contains("single"))
                    relation = s["single"].get<std::string>();
                // "by_words": the words before it or a marker give its relation (pl "{{por}} {{etymn|niem|waschen}}")
                if (parts.size() == 1 && s.value("by_words", false) && !relationOfWords.empty())
                    relation = relationOfWords;
                for (const auto &part : parts)
                    form += (form.empty() ? "" : " + ") + part;
            } else {
                form = termValue(param("term"));
                if (s.value("by_words", false) && !relationOfWords.empty() && !sameItem)
                    relation = relationOfWords;
            }
            if (lang.empty() || noTerm(form))
                continue;
            // "of": "entry": of the entry, not of the item before (pl {{źródło dla}}: the entry is its source)
            const int of = s.value("of", std::string()) == "entry" ? -1
                           : sameItem                             ? lastOf
                           : relation == "descendant"             ? last
                           : anchors.empty()                      ? -1
                                                                  : anchors.back();
            add(relation, lang, form, of);
        }
        chainDone |= chainHere;
    }
    std::string result;
    for (const auto &item : items)
        result += (result.empty() ? "" : "\n") + item;
    return result;
}

// Sections of the whole language, never of an entry
const std::set<std::string> languageNotes = {"References", "See also", "External links", "Further reading",
                                             "Anagrams", "Metagrams", "Bibliography", "Sources", "Literature"};

class Builder {
public:
    Builder(const json &config, const SectionNames &names, const LanguageCode &languageCode)
        : config_(config), names_(names), languageCode_(languageCode) {}

    json language(const json &in) {
        json out = {{"code", in["code"]}, {"language", in["language"]}, {"native", in["native"]}};
        language_ = &out;
        scopes_ = {Scope{}};
        const json root = {{"children", in.value("sections", json::array())}};
        plan(root, 0, -1);
        if (in.contains("text") || in.contains("media"))
            routeMedia(mediaItems(in.value("text", std::string()), in.value("media", json::array())), 0);
        // Before the first section, a pronunciation line: pl "{{wymowa}} {{IPA3|ˈvɔda}}"
        for (const auto &line : textLines(in.value("text", std::string())))
            if (auto all = wikilib::templates::find_invocations(line);
                !all.empty() && all.front().location.begin.offset == 0)
                if (auto name = names_.find(sectionKey(all.front().name).first); name && name->section == "Pronunciation")
                    append(out, "pronunciation", line);
        fill(root);
        // What a group gives its entries (en Etymology 2, a ru homograph): theirs when they have none
        for (size_t e = 0; e < entries_.size(); ++e)
            for (int s = entryScope_[e]; s > 0; s = scopes_[s].parent)
                for (const char *key : {"etymology", "pronunciation"})
                    if (!entries_[e].contains(key) && scopes_[s].given.contains(key))
                        entries_[e][key] = scopes_[s].given[key];
        json entries = json::array();
        const std::string code = out["code"].is_string() ? out["code"].get<std::string>() : std::string();
        for (auto &e : entries_) {
            // Translations by their language code, those of none last by the language as written, each
            // language in the order of the wiki; the language as written only of those without a code
            if (e.contains("translations")) {
                std::vector<json> list(e["translations"].begin(), e["translations"].end());
                std::ranges::stable_sort(list, [](const json &a, const json &b) {
                    if (a["lang"].is_null() != b["lang"].is_null())
                        return b["lang"].is_null();
                    if (a["lang"].is_null())
                        return a.value("lang_written", std::string()) < b.value("lang_written", std::string());
                    return a["lang"].get<std::string>() < b["lang"].get<std::string>();
                });
                for (auto &t : list)
                    if (!t["lang"].is_null())
                        t.erase("lang_written");
                e["translations"] = list;
            }
            addEtymologyChain(e, code);
            for (const char *key : {"senses", "translations", "see", "subpages", "media", "pos_also"})
                if (e.contains(key) && e[key].empty())
                    e.erase(key);
            for (const char *key : {"relations", "sections"})
                if (e.contains(key) && e[key].empty())
                    e.erase(key);
            entries.push_back(std::move(e));
        }
        if (!entries.empty())
            out["entries"] = std::move(entries);
        addEtymologyChain(out, code);
        for (const char *key : {"notes", "media"})
            if (out.contains(key) && out[key].empty())
                out.erase(key);
        return out;
    }

private:
    struct Scope {
        int parent = -1;
        std::string group;
        std::vector<size_t> entries; // its and of the scopes below
        int implicit = -1;           // the entry of senses outside any part of speech
        json given = json::object(); // etymology, pronunciation of the group
    };
    struct NodeInfo {
        int scope = 0, owner = -1;
        bool group = false; // en Etymology 2, a ru homograph: a scope of its own
    };
    const json &config_;
    const SectionNames &names_;
    const LanguageCode &languageCode_;
    json *language_ = nullptr;
    std::vector<Scope> scopes_;
    std::vector<json> entries_;
    std::vector<int> entryScope_;
    std::map<const json *, NodeInfo> info_;
    int lastEntry_ = -1;
    int homographs_ = 0;

    std::string kind(const json &node) const {
        if (!node.contains("section") || !node["section"].is_string())
            return "";
        auto name = names_.find(node["section"].get<std::string>());
        return name ? name->kind : "";
    }
    static std::string section(const json &node) { return node.value("section", std::string()); }
    static std::string text(const json &node) { return node.value("text", std::string()); }
    // (by reference: nodes are known by their address)
    static const json &children(const json &node) {
        static const json none = json::array();
        auto it = node.find("children");
        return it == node.end() ? none : *it;
    }

    static bool hasPos(const json &node, const Builder &b) {
        for (const auto &c : children(node))
            if (b.kind(c) == "pos" || hasPos(c, b))
                return true;
        return false;
    }

    size_t newEntry(int scope, json entry) {
        entries_.push_back(std::move(entry));
        entryScope_.push_back(scope);
        const size_t e = entries_.size() - 1;
        for (int s = scope; s >= 0; s = scopes_[s].parent)
            scopes_[s].entries.push_back(e);
        return e;
    }

    size_t implicitEntry(int scope) {
        if (scopes_[scope].implicit < 0) {
            json e = {{"pos", nullptr}};
            if (!scopes_[scope].group.empty())
                e["group"] = scopes_[scope].group;
            scopes_[scope].implicit = static_cast<int>(newEntry(scope, std::move(e)));
        }
        return static_cast<size_t>(scopes_[scope].implicit);
    }

    // Entries and groups in page order, before anything is routed to them
    void plan(const json &node, int scope, int owner) {
        for (const auto &c : children(node)) {
            int childScope = scope, childOwner = owner;
            bool group = false;
            if (kind(c) == "pos") {
                childOwner = static_cast<int>(newEntry(scope, posEntry(c, scope)));
            } else if (owner < 0 && (section(c) == "Homograph" || (c.contains("index") && hasPos(c, *this)))) {
                Scope s;
                s.parent = scope;
                s.group = section(c) == "Homograph" ? std::to_string(++homographs_) : c["index"].dump();
                scopes_.push_back(std::move(s));
                childScope = static_cast<int>(scopes_.size()) - 1;
                group = true;
            } else if (owner < 0 && section(c) == "Definitions" && c.contains("senses")) {
                implicitEntry(scope);
            }
            info_[&c] = {childScope, childOwner, group};
            plan(c, childScope, childOwner);
        }
    }

    json posEntry(const json &c, int scope) {
        // (a heading "part of speech" whose part of speech is not known says none: sat "*" not filled in)
        json e = {{"pos", c["section"] == "Part of speech" ? json() : c["section"]}};
        const auto heading = c.value("heading", std::string());
        e["pos_written"] = heading;
        std::string group = scopes_[scope].group;
        std::string grammar;
        json also = json::array();
        if (heading.starts_with("{{")) {
            auto all = wikilib::templates::find_invocations(heading);
            std::string rest = all.empty() ? "" : heading.substr(all.front().location.end.offset);
            // fr {{S|nom|fr|num=2}}
            if (!all.empty())
                if (auto n = all.front().get("num"); n && !trim(std::string(*n)).empty())
                    group = trim(std::string(*n));
            // de "{{Wortart|Substantiv|Deutsch}}, {{n}}, {{Wortart|Toponym|Deutsch}}": the others of
            // its parts of speech, the rest its grammar
            for (size_t i = 1; i < all.size(); ++i)
                if (auto v = all[i].get(size_t{1}); v && all[i].name == all.front().name)
                    if (auto name = names_.find(std::string(*v)); name && name->kind == "pos") {
                        also.push_back(name->section);
                        const auto written = heading.substr(all[i].location.begin.offset, all[i].location.length());
                        if (auto at = rest.find(written); at != std::string::npos)
                            rest.erase(at, written.size());
                    }
            std::vector<std::string> pieces;
            for (const auto &p : split(rest, ','))
                if (!trim(p).empty())
                    pieces.push_back(trim(p));
            for (const auto &p : pieces)
                grammar += (grammar.empty() ? "" : ", ") + p;
        } else if (auto comma = heading.find(','); comma != std::string::npos) {
            grammar = trim(heading.substr(comma + 1)); // pl "rzeczownik, rodzaj żeński"
        }
        // pl: the group of its sense ids, 2 of (2.1) (not of nested ids of "##", fr 7.1)
        if (group.empty() && str(config_, "/senses/marker").starts_with(": ("))
            for (const auto &s : c.value("senses", json::array()))
                if (auto id = s["id"].get<std::string>(); id.find('.') != std::string::npos) {
                    group = id.substr(0, id.find('.'));
                    break;
                }
        if (!group.empty())
            e["group"] = group;
        if (!grammar.empty())
            e["grammar"] = grammar;
        if (!also.empty())
            e["pos_also"] = also;
        // the first line of the text but a file or the lexeme (en {{wikidata lexeme|L3302}}), with the
        // lines of its template (de {{Deutsch Substantiv Übersicht …}})
        const auto lexemeTemplate = str(config_, "/senses/lexeme_template");
        std::string headword;
        int depth = 0;
        for (const auto &line : textLines(text(c))) {
            const auto t = trim(line);
            if (headword.empty()) {
                auto only = onlyTemplate(t);
                if (t.empty() || t.starts_with("[[") || (only && !lexemeTemplate.empty() && only->name == lexemeTemplate))
                    continue;
            }
            headword += (headword.empty() ? "" : "\n") + t;
            for (size_t i = 0; i + 1 < t.size(); ++i)
                if (t[i] == t[i + 1] && (t[i] == '{' || t[i] == '}'))
                    depth += t[i++] == '{' ? 1 : -1;
            if (depth <= 0)
                break;
        }
        if (!headword.empty())
            e["headword"] = headword;
        return e;
    }

    // fr {{S|nom|fr|num=2}} -> nom, 2: for etymology lines {{laé|fr|nom|2}}
    std::pair<std::string, std::string> key(size_t e) const {
        const auto heading = entries_[e].value("pos_written", std::string());
        if (!heading.starts_with("{{"))
            return {};
        auto all = wikilib::templates::find_invocations(heading);
        if (all.empty() || !all.front().get(size_t{1}))
            return {};
        return {std::string(*all.front().get(size_t{1})), std::string(all.front().get("num").value_or(""))};
    }

    std::vector<size_t> entriesOfIds(const std::vector<std::string> &ids, const std::vector<size_t> &among) const {
        std::vector<size_t> result;
        for (const auto &id : ids) {
            std::optional<size_t> found;
            for (auto e : among)
                for (const auto &s : entries_[e].value("senses", json::array()))
                    if (s["id"] == id && !found)
                        found = e;
            // pl (2.1): the entry of group 2
            if (!found && id.find('.') != std::string::npos)
                for (auto e : among)
                    if (entries_[e].value("group", std::string()) == id.substr(0, id.find('.')) && !found)
                        found = e;
            if (found && !std::ranges::contains(result, *found))
                result.push_back(*found);
        }
        return result;
    }

    // Lines of a section to the entries they are marked for (sense ids, fr {{laé|fr|nom|2}});
    // the unmarked rest
    std::pair<std::map<size_t, std::string>, std::string> splitByEntries(const std::string &text,
                                                                         const std::vector<size_t> &among) const {
        std::map<size_t, std::string> pieces;
        std::string rest;
        std::vector<size_t> current;
        for (const auto &line : textLines(text)) {
            std::vector<size_t> targets;
            if (auto mark = lineMark(line))
                targets = entriesOfIds(mark->ids, among);
            for (const auto &t : allTemplates(line))
                if (t.name == "laé")
                    for (auto e : among)
                        if (auto k = key(e); !k.first.empty() && t.get(size_t{2}) && k.first == *t.get(size_t{2}) &&
                                             (!t.get(size_t{3}) || k.second == *t.get(size_t{3})) &&
                                             !std::ranges::contains(targets, e))
                            targets.push_back(e);
            const auto t = trim(line);
            if (targets.empty() && !current.empty() && !t.empty() && t[0] != ':' && t[0] != '*' && t[0] != '#')
                targets = current; // a line of the template of a marked line (pl {{odmiana-…|…}})
            for (auto e : targets)
                pieces[e] += line + '\n';
            if (targets.empty())
                rest += line + '\n';
            current = t.empty() ? std::vector<size_t>() : targets;
        }
        return {pieces, rest};
    }

    void addRelation(size_t e, const std::string &name, json item) {
        auto &list = entries_[e]["relations"][name];
        if (!std::ranges::contains(list, item))
            list.push_back(std::move(item));
    }

    // etymology_chain of the etymology of an entry or a language; an etymology of no chain is
    // etymology_unparsed (there is one, not read yet: prose, a wiki of no etymology configuration), but
    // a placeholder of none (etymology.placeholders: en {{rfe}}, fr {{ébauche-étym}}, eu "Oraindik ez
    // daukagu…")
    void addEtymologyChain(json &object, const std::string &code) const {
        const auto text = object.contains("etymology") ? object["etymology"].get<std::string>() : std::string();
        if (trim(text).empty())
            return;
        if (auto chain = etymologyChain(text, config_, code, languageCode_); !chain.empty()) {
            object["etymology_chain"] = chain;
            return;
        }
        for (const auto &p : config_.value("etymology", json::object()).value("placeholders", std::vector<std::string>{}))
            if (text.find(p) != std::string::npos)
                return;
        object["etymology_unparsed"] = true;
    }

    // Files of a node but those of its translations (pl hieroglyphs: the image of a term)
    static json nodeMedia(const json &c) {
        json names = json::array();
        for (const auto &file : c.value("media", json::array())) {
            bool term = false;
            for (const auto &t : c.value("translations", json::array()))
                term |= t.value("image", std::string()) == file.get<std::string>();
            if (!term)
                names.push_back(file);
        }
        return mediaItems(text(c), names);
    }

    void routeMedia(const json &items, int scope, std::optional<size_t> entry = std::nullopt) {
        static const std::set<std::string> sounds = {"ogg", "oga", "wav", "mp3", "flac", "opus", "mid"};
        for (const auto &item : items) {
            // Sounds are of the pronunciation (pl {{audio|Pl-woda.ogg}}): "audio" of the entry or the language
            auto file = item["file"].get<std::string>();
            auto extension = wikilib::unicode::to_lower(file.substr(file.rfind('.') + 1));
            if (sounds.contains(extension)) {
                auto &audio = entry ? entries_[*entry]["audio"] : (*language_)["audio"];
                if (!std::ranges::contains(audio, json(file)))
                    audio.push_back(file);
                continue;
            }
            std::vector<size_t> targets;
            if (item.contains("sense"))
                targets = entriesOfIds(expandIds(item["sense"]), entry ? std::vector<size_t>{*entry} : scopes_[scope].entries);
            if (targets.empty() && entry)
                targets = {*entry};
            if (targets.empty())
                (*language_)["media"].push_back(item);
            for (auto e : targets)
                entries_[e]["media"].push_back(item);
        }
    }

    // Examples of a section by the sense ids of its lines (pl {{przykłady}} (1.2), de {{Beispiele}} [1]);
    // unmarked lines are returned
    std::string examples(const std::string &text, const std::vector<size_t> &among) {
        std::string rest;
        for (const auto &line : textLines(text)) {
            bool placed = false;
            if (auto mark = lineMark(line); mark && !trim(mark->rest).empty())
                for (const auto &id : mark->ids)
                    for (auto e : among)
                        for (auto &s : entries_[e]["senses"])
                            if (s["id"] == id) {
                                s["examples"].push_back({{"text", trim(mark->rest)}});
                                placed = true;
                            }
            if (!placed)
                rest += line + '\n';
        }
        return trim(rest).empty() ? "" : rest;
    }

    // Senses: examples of the definition's text (ru {{пример|…}}) and of its lines; relation
    // templates of its lines (en "#: {{syn|en|…}}") as relations of the entry
    void addSenses(size_t e, const json &senses) {
        const auto exampleTemplate = str(config_, "/senses/example_template");
        for (auto s : senses) {
            json examples = json::array();
            if (!exampleTemplate.empty()) {
                auto text = s["text"].get<std::string>();
                auto all = wikilib::templates::find_invocations(text);
                for (auto it = all.rbegin(); it != all.rend(); ++it)
                    if (it->name == exampleTemplate) {
                        text.erase(it->location.begin.offset, it->location.length());
                        if (auto v = it->get(size_t{1}); v && !trim(std::string(*v)).empty()) {
                            json example = {{"text", trim(std::string(*v))}};
                            std::string source;
                            for (size_t i = 2; i <= 4; ++i)
                                if (auto p = it->get(i); p && !trim(std::string(*p)).empty())
                                    source += (source.empty() ? "" : ", ") + trim(std::string(*p));
                            if (!source.empty())
                                example["source"] = source;
                            examples.insert(examples.begin(), std::move(example));
                        }
                    }
                s["text"] = trim(text);
            }
            for (const auto &x : s.value("examples", json::array())) {
                const auto written = x.get<std::string>();
                auto t = onlyTemplate(written);
                if (t && senseRelationTemplates().contains(t->name)) {
                    for (size_t i = 2; auto v = t->get(i); ++i)
                        if (auto term = termValue(std::string(*v)); !noTerm(term) && !term.starts_with("Thesaurus:"))
                            addRelation(e, senseRelationTemplates().at(t->name), {{"term", term}, {"sense", s["id"]}});
                    continue;
                }
                json example = {{"text", written}};
                if (t)
                    if (auto source = t->get("source"); source && !trim(std::string(*source)).empty())
                        example["source"] = trim(std::string(*source));
                examples.push_back(std::move(example));
            }
            s.erase("examples");
            if (!examples.empty())
                s["examples"] = std::move(examples);
            entries_[e]["senses"].push_back(std::move(s));
        }
    }

    void addTranslations(size_t e, const json &node) {
        for (const auto &t : node.value("translations", json::array()))
            entries_[e]["translations"].push_back(t);
        for (const auto &t : node.value("see", json::array()))
            entries_[e]["see"].push_back(t);
        if (node.value("subpage", false)) {
            json subpage = {{"title", node.value("source_title", std::string())}};
            if (node.contains("source"))
                subpage["source"] = node["source"];
            entries_[e]["subpages"].push_back(std::move(subpage));
        }
    }

    // ru: the part of speech of an entry without one from its morphology template ({{сущ ru m ina 3*a}})
    void posFromTemplate(const std::string &text, const std::vector<size_t> &among) {
        static const json::json_pointer prefixes("/pos/template_prefixes");
        if (!config_.contains(prefixes))
            return;
        for (const auto &t : wikilib::templates::find_invocations(text)) {
            const auto first = t.name.substr(0, t.name.find_first_of(" -")); // сущ ru m a, сущ-ru
            if (auto it = config_[prefixes].find(first); it != config_[prefixes].end()) {
                for (auto e : among)
                    if (entries_[e]["pos"].is_null())
                        entries_[e]["pos"] = *it;
                return;
            }
        }
    }

    // A section under a part of speech: of its entry
    void owned(const json &c, size_t e) {
        const auto name = section(c);
        addTranslations(e, c);
        if (c.contains("senses"))
            addSenses(e, c["senses"]);
        if (c.contains("lexeme"))
            entries_[e]["lexeme"] = c["lexeme"];
        routeMedia(nodeMedia(c), info_[&c].scope, e);
        if (name == "Translations" || name == "Definitions" || text(c).empty())
            return;
        if (name == "Etymology")
            append(entries_[e], "etymology", text(c));
        else if (name == "Pronunciation")
            append(entries_[e], "pronunciation", text(c));
        else if (name == "Examples") {
            if (auto rest = examples(text(c), {e}); !rest.empty())
                append(entries_[e]["sections"], name, rest);
        } else if (kind(c) == "relation" && !languageNotes.contains(name))
            for (auto &item : relationItems(text(c)))
                addRelation(e, name, std::move(item));
        else
            append(entries_[e]["sections"], name.empty() ? c.value("heading", std::string()) : name, text(c));
    }

    // A section above the parts of speech: to the entries of its scope it belongs to
    void shared(const json &c, int scope) {
        const auto name = section(c);
        const auto among = scopes_[scope].entries;
        if (name == "Definitions" && c.contains("senses"))
            addSenses(implicitEntry(scope), c["senses"]);
        for (const auto &t : c.value("translations", json::array())) {
            std::vector<size_t> targets;
            if (t.contains("sense"))
                if (auto ids = expandIds(t["sense"]); !ids.empty())
                    targets = entriesOfIds({ids.front()}, among);
            size_t e = !targets.empty()       ? targets.front()
                       : among.size() == 1    ? among.front()
                       : among.empty()        ? implicitEntry(scope)
                       : lastEntry_ >= 0 && std::ranges::contains(among, static_cast<size_t>(lastEntry_))
                               ? static_cast<size_t>(lastEntry_)
                               : among.front();
            entries_[e]["translations"].push_back(t);
        }
        if (c.contains("see") || c.value("subpage", false)) {
            json without = c;
            without.erase("translations");
            const bool last = lastEntry_ >= 0 && std::ranges::contains(among, static_cast<size_t>(lastEntry_));
            addTranslations(among.empty() ? implicitEntry(scope) : last ? static_cast<size_t>(lastEntry_) : among.front(),
                            without);
        }
        if (c.contains("lexeme"))
            for (auto e : among)
                entries_[e]["lexeme"] = c["lexeme"];
        routeMedia(nodeMedia(c), scope);
        if (name == "Translations" || name == "Definitions" || text(c).empty())
            return;
        posFromTemplate(text(c), among);
        if (name == "Etymology" || name == "Pronunciation") {
            const std::string key = name == "Etymology" ? "etymology" : "pronunciation";
            auto [pieces, rest] = splitByEntries(text(c), among);
            for (const auto &[e, piece] : pieces)
                append(entries_[e], key, piece);
            if (!trim(rest).empty()) {
                if (scope > 0)
                    append(scopes_[scope].given, key, rest);
                else
                    append(*language_, key, rest);
            }
            return;
        }
        if (languageNotes.contains(name) || name.empty()) {
            append((*language_)["notes"], name.empty() ? c.value("heading", std::string()) : name, text(c));
            return;
        }
        if (kind(c) == "relation") {
            for (auto &item : relationItems(text(c))) {
                std::vector<size_t> targets;
                if (item.contains("sense"))
                    targets = entriesOfIds(expandIds(item["sense"]), among);
                if (targets.empty())
                    targets = among; // of all its entries (pl {{pokrewne}} without ids)
                for (auto e : targets)
                    addRelation(e, name, item);
            }
            return;
        }
        std::string rest = text(c);
        if (name == "Examples")
            rest = examples(rest, among);
        auto [pieces, unmarked] = splitByEntries(rest, among);
        for (const auto &[e, piece] : pieces)
            append(entries_[e]["sections"], name, piece);
        if (!trim(unmarked).empty()) {
            if (among.size() == 1)
                append(entries_[among.front()]["sections"], name, unmarked);
            else
                append((*language_)["notes"], name, unmarked);
        }
    }

    void fill(const json &node) {
        for (const auto &c : children(node)) {
            const auto [scope, owner, group] = info_[&c];
            if (kind(c) == "pos") {
                const auto e = static_cast<size_t>(owner);
                lastEntry_ = static_cast<int>(e);
                if (c.contains("senses"))
                    addSenses(e, c["senses"]);
                addTranslations(e, c);
                if (c.contains("lexeme"))
                    entries_[e]["lexeme"] = c["lexeme"];
                routeMedia(nodeMedia(c), scope, e);
            } else if (owner >= 0) {
                owned(c, static_cast<size_t>(owner));
            } else if (group) {
                // a group (en Etymology 2, a ru homograph): its text is of its entries
                if (section(c) == "Etymology")
                    append(scopes_[scope].given, "etymology", text(c));
                else if (section(c) == "Pronunciation")
                    append(scopes_[scope].given, "pronunciation", text(c));
                if (c.contains("lexeme"))
                    for (auto e : scopes_[scope].entries)
                        entries_[e]["lexeme"] = c["lexeme"];
                routeMedia(nodeMedia(c), scope);
            } else {
                shared(c, scope);
            }
            fill(c);
        }
    }
};

// The page of convertPage in the uniform tree
json page(const json &converted, const json &config, const SectionNames &names, const LanguageCode &languageCode) {
    json languages = json::array();
    for (const auto &language : converted["sections"])
        languages.push_back(Builder(config, names, languageCode).language(language));
    return {{"wiki", converted["wiki"]}, {"title", converted["title"]}, {"ns", converted["ns"]},
            {"source", converted["source"]}, {"languages", std::move(languages)}};
}

} // namespace uniform

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
            // "Референце"); counted for review, the section goes on. The first heading of a page,
            // before any language, that is no section either: a language of its own, code null
            // (it takes no entry of a known language, and its entry is not lost)
            if (!knownSections[wiki].contains(*marker)) {
                auto &[markerCode, count] = stats.markers[*marker];
                ++count;
                stats.markerExamples.emplace(*marker, title);
            }
            // (not of wikis whose pages without a language marker are of the wiki's language,
            // fallback_native: there the first heading is rather a section of it, nds, mr)
            if (current || config["language"].value("fallback_native", false) ||
                knownSections[wiki].contains(*marker) || sectionNames.find(sectionKey(*marker).first))
                marker.reset();
        }
        // Of a code form with names as another form (yue, az "also": heading-name): a heading that is
        // no language but a section of the wiki (yue "== 參考 ==", az "== Tərcümələr ==") is a section
        if (marker && !byName && !implicit &&
            !(config.contains(json::json_pointer("/language/aliases")) && config["language"]["aliases"].contains(*marker)) &&
            !names.isCode(*marker) && names.wikiCode(wiki, *marker).empty() && names.mappedCode(*marker).empty() &&
            markerCode(*marker).empty() && sectionNames.find(sectionKey(*marker).first))
            marker.reset();
        // In an open language section, a {{-…-}} marker that is a code and a section of the wiki
        // (vec {{-syn-}} synonyms, not Senaya; {{-rel-}} related terms, not Rendille) is a section,
        // unless the configuration names it a language
        if (marker && current && !byName && !implicit && dashTemplate(line) == 1 &&
            !(config.contains(json::json_pointer("/language/aliases")) && config["language"]["aliases"].contains(*marker)) &&
            sectionNames.find(sectionKey(trim(line)).first, 1))
            marker.reset();
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
            // A marker that is no known language (jbo "TLI", su "bahasa"): a language of its own, code null
            if (!code.empty() && !names.isCode(code) && names.mappedCode(code).empty())
                code.clear();
            auto &[markerCode, count] = stats.markers[*marker];
            markerCode = code;
            ++count;
            stats.markerExamples.emplace(*marker, title);
            // A section of nothing followed by a marker of its language (la {{caput|la|aqua}} then
            // =={{-lingua-|la|aqua}}==): one section, of the later marker
            if (current && current->value("sections", json::array()).empty() && !current->contains("text") &&
                !current->contains("media") && (*current)["code"] == (code.empty() ? json() : json(code))) {
                stats.sections--;
                stats.sectionsWithCode -= !code.empty();
                stats.nativeSections -= (*current)["native"].get<bool>();
                sections.erase(sections.end() - 1);
            }
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

// Named HTML entities beyond those of wikilib (res/html_entities.tsv: &euml; ë, &larr; ←, &alpha; α)
std::map<std::string, std::string> htmlEntities;

// An HTML entity ("&#171;", "&euml;") as its character; nullopt when it is none
std::optional<std::string> decodedEntity(std::string_view entity) {
    if (entity.size() > 2)
        if (auto it = htmlEntities.find(std::string(entity.substr(1, entity.size() - 2))); it != htmlEntities.end())
            return it->second;
    auto decoded = wikilib::text::decode_html_entities(entity);
    return decoded != entity ? std::optional(decoded) : std::nullopt;
}

// The text with its HTML entities decoded; keep(character, place) says which to leave as they are
std::string decodedEntities(const std::string &text, const std::function<bool(const std::string &, size_t)> &keep) {
    if (text.find('&') == std::string::npos)
        return text;
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '&')
            if (const size_t end = text.find(';', i + 1); end != std::string::npos && end - i < 12)
                if (auto decoded = decodedEntity(std::string_view(text).substr(i, end - i + 1));
                    decoded && !keep(*decoded, i)) {
                    result += *decoded;
                    i = end + 1;
                    continue;
                }
        result += text[i++];
    }
    return result;
}

// The page text for parsing with its HTML entities decoded (ru "символ &#171;@&#187;" -> "символ «@»",
// &nbsp;), but those of a character of wiki markup, which the writers put there not to make markup
// (&#124; |, &#61; =, &lt; <, &#91; [ … stay)
std::string withDecodedEntities(std::string text) {
    static const std::string_view markup = "<>&[]{}|=':*#;~\n\r";
    return decodedEntities(text, [](const std::string &c, size_t) {
        return c.size() == 1 && markup.find(c[0]) != std::string_view::npos;
    });
}

// A string of the output with all its HTML entities decoded (etymology "A &gt; B", "&#91;1&#93;"), but,
// inside a template or a link, those of its markup (&#124; | {{t|en|a&#124;b}}, &#61; =, brackets),
// so that the wikitext of a field reads as before
std::string outputText(const std::string &text) {
    if (text.find('&') == std::string::npos)
        return text;
    // depth of {{ }} and [[ ]] at each place
    std::vector<int> depth(text.size() + 1, 0);
    int d = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        depth[i] = d;
        if (i + 1 < text.size() && (text.compare(i, 2, "{{") == 0 || text.compare(i, 2, "[[") == 0))
            ++d;
        else if (i + 1 < text.size() && (text.compare(i, 2, "}}") == 0 || text.compare(i, 2, "]]") == 0) && d > 0)
            --d;
    }
    return decodedEntities(text, [&](const std::string &c, size_t at) {
        return depth[at] > 0 && c.size() == 1 && std::string_view("|=[]{}").find(c[0]) != std::string_view::npos;
    });
}

// The output page with outputText in all its strings and keys
json withOutputText(const json &v) {
    if (v.is_string())
        return outputText(v.get<std::string>());
    if (v.is_array()) {
        json out = json::array();
        for (const auto &x : v)
            out.push_back(withOutputText(x));
        return out;
    }
    if (v.is_object()) {
        json out = json::object();
        for (const auto &[k, x] : v.items())
            out[outputText(k)] = withOutputText(x);
        return out;
    }
    return v;
}

// The output: tools/jsonl_blocks.h
using jsonlblocks::Compression;
using jsonlblocks::JsonlWriter;

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
        bool fromDumps = false, markersOnly = false, uniformTree = false;
        Compression compression = Compression::none;
        int level = 0; // 0: of the format (bz2 9, zstd 9)
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
            else if (arg == "--uniform")
                uniformTree = true;
            else if (arg == "--markers-only")
                markersOnly = true;
            else if (arg == "--bz2")
                compression = Compression::bz2;
            else if (arg == "--zstd")
                compression = Compression::zstd;
            else if (arg == "--level" && i + 1 < argc)
                level = std::stoi(argv[++i]);
            else if (arg == "-j" && i + 1 < argc)
                threads = std::max(1, std::stoi(argv[++i]));
            else if (arg == "-h" || arg == "--help") {
                std::cout << "usage: wikt2jsonl [-i samples100 | --dump [-j N]] [--markers-only] [--uniform] [--bz2 | --zstd [--level N]] [-o jsonl] [--db wiktlangs.sqlite] [wiki...]\n"
                             "       --uniform: pages in the uniform tree (doc/uniform-tree.md)\n"
                             "       --dump: all pages of the dumps, each wiki read in N threads (default 8)\n"
                             "       --markers-only: only language markers and stats, no JSONL\n"
                             "       --bz2, --zstd: <wiki>.jsonl.bz2 or .jsonl.zst in blocks of 100 lines or 512 KB\n"
                             "              with <wiki>.jsonl.idx; --level: bz2 1-9, zstd 1-22 (9);\n"
                             "              zstd with a dictionary <wiki>.jsonl.dict of the first 32 MB\n";
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
        for (const auto &line : readLines((configDir.parent_path() / "html_entities.tsv").string()))
            if (auto f = split(line, '\t'); f.size() == 2 && f[0] != "name" && !f[0].starts_with("#"))
                htmlEntities[f[0]] = f[1];
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
        std::string header = "wiki\tprofile\tpages\twith_sections\tsections\twith_code\tsenses\ttranslations\tnative_sections\tnative_senses\tnative_translations\tseconds";
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
            // pl: a template of the wiki maps the abbreviations of languages of its etymologies to names
            // (Szablon:etym/język: "|niem=Język niemiecki"); they become codes (etymology.language_names)
            if (auto page = str(config, "/etymology/language_switch"); !page.empty()) {
                auto pages = WikiDump::wiktionary(wiki).extractMatching([&](const std::string &t) { return t == page; });
                size_t added = 0;
                if (auto it = pages.find(page); it != pages.end()) {
                    static const std::regex option(R"(^\s*\|([^=|{}]+)=([^|{}<]*))");
                    for (const auto &line : uniform::textLines(it->second)) {
                        std::smatch m;
                        if (line.size() > 500 || !std::regex_search(line, m, option))
                            continue;
                        const auto key = trim(m[1].str()), name = wikilib::unicode::to_lower(trim(m[2].str()));
                        auto codeOf = [&](const std::string &n) {
                            return names.code(wiki, n, str(config, "/language/lookup"),
                                              config["language"].value("strip", std::vector<std::string>{}));
                        };
                        // (the name, else its last words: "Język prazachodniogermański")
                        std::string code = codeOf(name);
                        if (auto space = name.find(' '); code.empty() && space != std::string::npos)
                            code = codeOf(trim(name.substr(space + 1)));
                        if (!key.empty() && !code.empty() && !config["etymology"]["language_names"].contains(key)) {
                            config["etymology"]["language_names"][key] = code;
                            ++added;
                        }
                    }
                }
                std::cout << wiki << ": " << added << " etymology language abbreviations from " << page << std::endl;
            }
            if (config.value("skip", false))
                continue;
            const SectionNames sectionNames("wiktsections.sqlite", wiki);
            // A language as written in an etymology (a code, a name of the wiki) as its code, else empty
            auto languageCode = [&](const std::string &written) -> std::string {
                if (names.isCode(written))
                    return written;
                if (auto mapped = names.mappedCode(written); !mapped.empty())
                    return mapped;
                return names.code(wiki, wikilib::unicode::to_lower(written), str(config, "/language/lookup"),
                                  config["language"].value("strip", std::vector<std::string>{}));
            };
            auto written = [&](const json &page) {
                return withOutputText(uniformTree ? uniform::page(page, config, sectionNames, languageCode) : page);
            };
            const auto wikiStarted = std::chrono::steady_clock::now();
            Stats stats;
            const size_t workers =
                    fromDumps ? wikilib::dump::parallel_worker_count(
                                        WikiDump::dumpPath(wikilib::dump::WikiProject::Wiktionary, wiki), threads)
                              : 1;
            std::optional<JsonlWriter> out;
            if (!markersOnly)
                // (the compressed output of a dump: some 0.6 of the dump; pl 109 of 181 MB)
                out.emplace(output / wiki, compression, level ? level : 9, workers,
                            fromDumps ? WikiDump::dumpPath(wikilib::dump::WikiProject::Wiktionary, wiki).dump_size() * 6 / 10 : 0);
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
                                                    withDecodedEntities(wikilib::markup::strip_comments(content)),
                                                    workerStats[worker], markersOnly,
                                                    json::array({location.block, location.index}));
                            // Pages without a recognized language are left out
                            if (out && !line["sections"].empty())
                                out->add(worker, written(line).dump(-1, ' ', false, json::error_handler_t::replace), title);
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
                                        withDecodedEntities(wikilib::markup::strip_comments(text.str())), stats,
                                        markersOnly,
                                        page.value("source", json()));
                if (out && !line["sections"].empty()) // pages without a recognized language are left out
                    out->add(0, written(line).dump(-1, ' ', false, json::error_handler_t::replace), page["title"]);
            }
            if (out)
                out->close(); // (its last blocks: in the time of the wiki)
            auto row = std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{:.1f}", wiki, config.value("profile", ""),
                                   stats.pages, stats.pagesWithSections, stats.sections, stats.sectionsWithCode, stats.senses,
                                   stats.translations, stats.nativeSections, stats.nativeSenses, stats.nativeTranslations,
                                   std::chrono::duration<double>(std::chrono::steady_clock::now() - wikiStarted).count());
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
