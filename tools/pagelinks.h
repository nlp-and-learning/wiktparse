// pagelinks: the translations of a page linked to the pages of their terms (wiktlink, and wikt2jsonl
// --uniform before it writes a page): "page", "home", "en", "page_title" from the (title, language)
// pairs of every wiki; translation subpages (en water/translations) into their base page
#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace pagelinks {

namespace fs = std::filesystem;
using json = nlohmann::json;

inline uint64_t key(const std::string &title, const std::string &code) {
    return std::hash<std::string>{}(title + '\x1f' + code);
}

// Codes of a wiki's own language (as nativeCodes of wikt2jsonl)
inline std::vector<std::string> nativeCodes(const std::string &wiki) {
    static const std::map<std::string, std::vector<std::string>> special = {
            {"simple", {}}, {"zh_min_nan", {"nan", "nan-hbl"}}, {"no", {"no", "nb"}}, {"roa_rup", {"rup"}}};
    if (auto it = special.find(wiki); it != special.end())
        return it->second;
    std::string code = wiki;
    std::ranges::replace(code, '_', '-');
    return {code};
}

// Every item of the lists of a key ("translations", "see") anywhere in a page: of the tree of
// headings (nodes of sections, children) and of the uniform tree (languages, entries)
inline void eachOf(json &node, const char *key, const std::function<void(json &)> &f) {
    if (node.is_object()) {
        for (auto &[k, v] : node.items())
            if (k == key && v.is_array()) {
                for (auto &item : v)
                    f(item);
            } else if (v.is_structured())
                eachOf(v, key, f);
    } else if (node.is_array())
        for (auto &item : node)
            eachOf(item, key, f);
}

inline void eachTranslation(json &node, const std::function<void(json &)> &f) { eachOf(node, "translations", f); }

// The language sections of a page: "languages" of the uniform tree, "sections" of the tree of headings
inline json &languagesOf(json &page) { return page[page.contains("languages") ? "languages" : "sections"]; }
inline const json &languagesOf(const json &page) {
    static const json none = json::array();
    auto it = page.find(page.contains("languages") ? "languages" : "sections");
    return it == page.end() ? none : *it;
}

// The term as a page title: without an interwiki prefix (kn ":en:fire"), {{PAGENAME}} as the
// page's title (el), links as their text ("[[ristiä]] [[miekka|miekat]]" -> "ristiä miekat")
inline std::string termTitle(std::string term, const std::string &pageTitle) {
    if (auto at = term.find("{{PAGENAME}}"); at != std::string::npos)
        term.replace(at, 12, pageTitle);
    std::string plain;
    for (size_t i = 0; i < term.size();) {
        if (term.compare(i, 2, "[[") == 0) {
            const size_t end = term.find("]]", i + 2);
            if (end == std::string::npos)
                break;
            std::string link = term.substr(i + 2, end - i - 2);
            if (auto bar = link.rfind('|'); bar != std::string::npos)
                link = link.substr(bar + 1);
            plain += link;
            i = end + 2;
        } else
            plain += term[i++];
    }
    // ":en:enter fire", "en:water": an interwiki prefix
    if (plain.starts_with(':'))
        plain.erase(0, 1);
    if (auto colon = plain.find(':'); colon != std::string::npos && colon >= 2 && colon <= 3 &&
                                      std::all_of(plain.begin(), plain.begin() + static_cast<long>(colon),
                                                  [](unsigned char c) { return c >= 'a' && c <= 'z'; }))
        plain.erase(0, colon + 1);
    while (!plain.empty() && (plain.front() == ' ' || plain.back() == ' '))
        plain.front() == ' ' ? plain.erase(0, 1) : plain.erase(plain.size() - 1);
    return plain;
}

// The term without marks that page titles leave out: combining accents (ru "кана́тная"
// U+0301), Arabic harakat, Hebrew points, Latin macrons and breves (la "aqua" of "ăquā")
inline std::string withoutMarks(const std::string &text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        const auto c0 = static_cast<unsigned char>(text[i]);
        const size_t length = c0 < 0x80 ? 1 : c0 < 0xE0 ? 2 : c0 < 0xF0 ? 3 : 4;
        uint32_t cp = length == 1 ? c0 : c0 & (0x7F >> length);
        for (size_t k = 1; k < length && i + k < text.size(); ++k)
            cp = cp << 6 | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        const bool mark = (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x064B && cp <= 0x0652) || cp == 0x0670 ||
                          (cp >= 0x0591 && cp <= 0x05C7);
        // Latin vowels with macron or breve: the vowel
        static const std::map<uint32_t, char> latin = {
                {0x0100, 'A'}, {0x0101, 'a'}, {0x0102, 'A'}, {0x0103, 'a'}, {0x0112, 'E'}, {0x0113, 'e'}, {0x0114, 'E'},
                {0x0115, 'e'}, {0x012A, 'I'}, {0x012B, 'i'}, {0x012C, 'I'}, {0x012D, 'i'}, {0x014C, 'O'}, {0x014D, 'o'},
                {0x014E, 'O'}, {0x014F, 'o'}, {0x016A, 'U'}, {0x016B, 'u'}, {0x016C, 'U'}, {0x016D, 'u'}, {0x0232, 'Y'},
                {0x0233, 'y'}};
        if (auto it = latin.find(cp); it != latin.end())
            out += it->second;
        else if (!mark)
            out.append(text, i, length);
        i += length;
    }
    return out;
}

// Languages whose entries stand under another code: en.wikt ==Chinese== (zh) for the cmn, yue
// translations; Norwegian (no) for nb, nn
inline const std::vector<std::string> &equivalents(const std::string &code) {
    static const std::map<std::string, std::vector<std::string>> map = [] {
        std::map<std::string, std::vector<std::string>> m;
        for (const char *c : {"cmn", "yue", "wuu", "hak", "nan", "gan", "hsn", "cdo", "cjy", "cpx", "czh", "mnp", "cnp",
                              "csp", "zh-cn", "zh-tw", "zh-hans", "zh-hant"})
            m[c] = {"zh"};
        m["zh"] = {"cmn"};
        m["nb"] = {"no"};
        m["nn"] = {"no"};
        m["no"] = {"nb", "nn"};
        return m;
    }();
    static const std::vector<std::string> none;
    auto it = map.find(code);
    return it == map.end() ? none : it->second;
}

// Translation subpages: en "water/translations", zh "水/翻譯" (the base page links to them)
inline const std::vector<std::string> subpageSuffixes = {"/translations", "/翻譯"};

inline std::string subpageBase(const std::string &title) {
    for (const auto &suffix : subpageSuffixes)
        if (title.size() > suffix.size() && title.ends_with(suffix))
            return title.substr(0, title.size() - suffix.size());
    return {};
}

// The translations of a subpage into its base page of the uniform tree: each entry of the subpage
// with translations into the first entry of its language and part of speech on the page without
// translations of its own (en water/translations, Noun: water, Noun), with "subpages": its title and
// place in the dump; of none such a new entry of them, not lost; the number merged
inline size_t mergeUniformSubpage(json &page, const json &subpage) {
    size_t merged = 0;
    for (const auto &subLanguage : subpage.value("languages", json::array()))
        for (const auto &from : subLanguage.value("entries", json::array())) {
            if (!from.contains("translations") && !from.contains("see"))
                continue;
            json *to = nullptr, *language = nullptr;
            for (auto &l : page["languages"]) {
                if (l.value("code", json()) != subLanguage.value("code", json()))
                    continue;
                language = &l;
                if (auto entries = l.find("entries"); entries != l.end())
                    for (auto &e : *entries)
                        if (e.value("pos", json()) == from.value("pos", json()) && !e.contains("translations")) {
                            to = &e;
                            break;
                        }
                break;
            }
            if (!language)
                continue; // (no section of the language on the page: the subpage stays on its own)
            if (!to) {
                (*language)["entries"].push_back({{"pos", from.value("pos", json())}});
                to = &(*language)["entries"].back();
            }
            for (const char *key : {"translations", "see"})
                for (const auto &t : from.value(key, json::array()))
                    (*to)[key].push_back(t);
            json source = {{"title", subpage.value("title", "")}};
            if (subpage.contains("source") && !subpage["source"].is_null())
                source["source"] = subpage["source"];
            (*to)["subpages"].push_back(std::move(source));
            ++merged;
        }
    return merged;
}

// Every reference to the translations of other pages ("see" of wikt2jsonl)
inline void eachSee(json &node, const std::function<void(json &)> &f) { eachOf(node, "see", f); }

// Pages of reconstructed words: their form without "*" ("watōr"), empty of another page. A page of a
// namespace "<ns>:<language>/<form>" of a section of a proto-language (a code "-pro": en
// "Reconstruction:Proto-Germanic/watōr", ja "付録:…", fa "بازساخت:…", vi "Từ tái tạo:…"), of en, zh, fr
// "Reconstruction:" of any language (en "Reconstruction:Latin/…" of Vulgar Latin); a page of the main
// namespace "*<form>" of a proto-language (ru "*(j)azъ", sla-pro; es, sv)
inline std::string reconstructionForm(const std::string &title, const std::vector<std::string> &codes) {
    const bool proto = std::ranges::any_of(codes, [](const std::string &c) { return c.ends_with("-pro"); });
    std::string form;
    const auto colon = title.find(':'), slash = title.find('/');
    if (colon != std::string::npos && slash != std::string::npos && colon < slash && slash + 1 < title.size() &&
        (proto || title.starts_with("Reconstruction:") || title.starts_with("Rekonstruktion:")))
        form = title.substr(slash + 1);
    else if (proto && title.starts_with('*') && colon == std::string::npos)
        form = title;
    while (form.starts_with('*'))
        form.erase(0, 1);
    return form;
}

// The (title, language) pairs of the pages of every wiki: whether a wiki has a page of a term with a
// section of its language; in memory as hashes, sorted (some 55 million of all wikis: 440 MB). Of a
// pass over the output (wiktlink) or of the language pass of wikt2jsonl, saved per wiki as the text
// <wiki>.languages: a line a page, its title, a tab, the codes of its sections ("Wasser\tde,nds,lb"),
// after a first line of the dump it is of, "# <file name>\t<size>": of another dump it is made again
struct Pairs {
    std::map<std::string, std::vector<uint64_t>> sections; // wiki -> its keys
    std::map<std::string, std::string> homeWiki;            // language code -> the Wiktionary of it
    // wiki -> (form without "*", code) -> the title of its page of a reconstructed word
    std::map<std::string, std::unordered_map<uint64_t, std::string>> reconstructions;

    void add(const std::string &wiki, std::vector<uint64_t> keys) {
        std::ranges::sort(keys);
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        sections[wiki] = std::move(keys);
    }
    // The first line of a file of the languages of a dump: the dump's file name and size
    static std::string dumpLine(const fs::path &dump) {
        std::error_code error;
        return "# " + dump.filename().string() + '\t' + std::to_string(fs::file_size(dump, error));
    }
    // A file of the languages of this dump (its first line that of the dump)
    static bool ofDump(const fs::path &file, const fs::path &dump) {
        std::ifstream in(file);
        std::string first;
        return std::getline(in, first) && first == dumpLine(dump);
    }
    // title -> codes, in title order, after the line of the dump
    static void save(const fs::path &file, const fs::path &dump, const std::map<std::string, std::vector<std::string>> &codes) {
        std::ofstream out(file);
        out << dumpLine(dump) << '\n';
        for (const auto &[title, list] : codes) {
            out << title << '\t';
            for (size_t i = 0; i < list.size(); ++i)
                out << (i ? "," : "") << list[i];
            out << '\n';
        }
    }
    void load(const std::string &wiki, const fs::path &file) {
        std::ifstream in(file);
        std::vector<uint64_t> keys;
        for (std::string line; std::getline(in, line);) {
            const auto tab = line.rfind('\t');
            if (tab == std::string::npos || line.starts_with("# "))
                continue;
            const std::string title = line.substr(0, tab);
            std::vector<std::string> codes;
            for (size_t from = tab + 1; from < line.size();) {
                const auto comma = std::min(line.find(',', from), line.size());
                if (comma > from)
                    codes.push_back(line.substr(from, comma - from));
                from = comma + 1;
            }
            const std::string form = reconstructionForm(title, codes);
            for (const auto &code : codes) {
                keys.push_back(key(title, code));
                if (!form.empty())
                    reconstructions[wiki][key(form, code)] = title;
            }
        }
        add(wiki, std::move(keys));
    }
    // The Wiktionary of each language, of the wikis known (cmn: zh.wiktionary)
    void finish() {
        homeWiki.clear();
        for (const auto &[wiki, keys] : sections)
            for (const auto &code : nativeCodes(wiki))
                homeWiki.emplace(code, wiki);
        for (const auto &[code, wiki] : std::map<std::string, std::string>(homeWiki))
            for (const auto &other : equivalents(code))
                homeWiki.emplace(other, wiki);
    }
    [[nodiscard]] size_t size() const {
        size_t n = 0;
        for (const auto &[wiki, keys] : sections)
            n += keys.size();
        return n;
    }
    [[nodiscard]] bool has(const std::string &wiki, const std::string &title, const std::string &code) const {
        auto it = sections.find(wiki);
        return it != sections.end() && std::ranges::binary_search(it->second, key(title, code));
    }
    // The title of a page of the term with a section of the language (or an equivalent one): exact,
    // without marks, with optional parts in brackets left out or in (de "(an)schließen":
    // "schließen", "anschließen"); empty for none
    [[nodiscard]] std::string find(const std::string &wiki, const std::string &term, const std::string &code) const {
        std::vector<std::string> titles{term};
        auto add = [&](std::string t) {
            while (t.find("  ") != std::string::npos)
                t.replace(t.find("  "), 2, " ");
            while (!t.empty() && t.front() == ' ')
                t.erase(0, 1);
            while (!t.empty() && t.back() == ' ')
                t.pop_back();
            if (!t.empty() && !std::ranges::contains(titles, t))
                titles.push_back(std::move(t));
        };
        add(withoutMarks(term));
        if (term.find('(') != std::string::npos) {
            std::string without, with;
            int depth = 0;
            for (char ch : term) {
                if (ch == '(')
                    ++depth;
                else if (ch == ')')
                    depth = std::max(0, depth - 1);
                else {
                    with += ch;
                    if (!depth)
                        without += ch;
                }
            }
            add(without);
            add(with);
        }
        for (const auto &title : titles) {
            if (has(wiki, title, code))
                return title;
            for (const auto &other : equivalents(code))
                if (has(wiki, title, other))
                    return title;
        }
        return {};
    }
};

struct LinkCount {
    size_t translations = 0, page = 0, home = 0, both = 0, none = 0, noLang = 0, noTerm = 0, subpages = 0, see = 0,
           seeFound = 0, en = 0, etymologyItems = 0, etymologyLinked = 0;
    void add(const LinkCount &c) {
        translations += c.translations, page += c.page, home += c.home, both += c.both, none += c.none;
        noLang += c.noLang, noTerm += c.noTerm, subpages += c.subpages, see += c.see, seeFound += c.seeFound, en += c.en;
        etymologyItems += c.etymologyItems, etymologyLinked += c.etymologyLinked;
    }
};

// The items of an etymology chain linked to the pages of their words ("etymology_pages" of a node with
// "etymology_chain": item number -> "<wiki>:<title>"): of a reconstructed word ("*watōr") its page of
// reconstruction (en "Reconstruction:Proto-Germanic/watōr"), else a page of the word with a section of
// its language; of this wiki, else of the Wiktionary of the language, else of en.wiktionary. Items of
// no word (uncertain, attested) and compounds are left
inline void linkEtymology(json &node, const std::string &wiki, const Pairs &pairs, LinkCount &c) {
    const auto chain = node.find("etymology_chain");
    if (chain == node.end() || !chain->is_string())
        return;
    json links = json::object();
    for (size_t from = 0; from < chain->get_ref<const std::string &>().size();) {
        const auto &text = chain->get_ref<const std::string &>();
        const auto end = std::min(text.find('\n', from), text.size());
        std::string line = text.substr(from, end - from);
        from = end + 1;
        // "4. from gem-pro *watōr [2] ?"
        const auto dot = line.find(". ");
        if (dot == std::string::npos)
            continue;
        const std::string number = line.substr(0, dot);
        line = line.substr(dot + 2);
        if (line.ends_with(" ?"))
            line.resize(line.size() - 2);
        if (const auto of = line.rfind(" ["); of != std::string::npos && line.ends_with("]"))
            line.resize(of);
        const auto space = line.find(' '), second = line.find(' ', space == std::string::npos ? space : space + 1);
        if (space == std::string::npos || second == std::string::npos)
            continue;
        const std::string relation = line.substr(0, space), lang = line.substr(space + 1, second - space - 1);
        const std::string form = line.substr(second + 1);
        if (relation == "compound" || form.empty())
            continue;
        ++c.etymologyItems;
        auto pageOf = [&](const std::string &target) -> std::string {
            if (form.starts_with('*')) {
                const auto it = pairs.reconstructions.find(target);
                if (it == pairs.reconstructions.end())
                    return {};
                for (const auto &f : {form.substr(1), withoutMarks(form.substr(1))})
                    if (auto r = it->second.find(key(f, lang)); r != it->second.end())
                        return r->second;
                return {};
            }
            return pairs.find(target, form, lang);
        };
        std::vector<std::string> targets{wiki};
        if (auto home = pairs.homeWiki.find(lang); home != pairs.homeWiki.end() && home->second != wiki)
            targets.push_back(home->second);
        if (wiki != "en")
            targets.emplace_back("en");
        for (const auto &target : targets)
            if (const auto title = pageOf(target); !title.empty()) {
                links[number] = target + ":" + title;
                ++c.etymologyLinked;
                break;
            }
    }
    if (!links.empty())
        node["etymology_pages"] = std::move(links);
}

// The translations of a page of a wiki linked: "page" (the wiki has a page of the term with a section of
// its language), "home" (the Wiktionary of the language has one), "en" (en.wiktionary has one),
// "page_title" (the title found when it is not the term); a reference to the translations of another
// page ("see") "page" when that page has a section of the language
inline void linkPage(json &page, const std::string &wiki, const Pairs &pairs, LinkCount &c) {
    const std::string title = page.value("title", std::string());
    for (auto &s : languagesOf(page))
        if (s.contains("code") && s["code"].is_string())
            eachSee(s, [&](json &e) {
                ++c.see;
                if (pairs.has(wiki, termTitle(e.value("title", std::string()), title), s["code"].get<std::string>())) {
                    e["page"] = true;
                    ++c.seeFound;
                }
            });
    for (auto &s : languagesOf(page))
        eachTranslation(s, [&](json &t) {
            ++c.translations;
            if (!t.contains("lang") || !t["lang"].is_string()) {
                ++c.noLang;
                return;
            }
            const std::string lang = t["lang"].get<std::string>();
            const std::string term = t.contains("term") && t["term"].is_string() ? termTitle(t["term"].get<std::string>(), title) : "";
            if (term.empty()) {
                ++c.noTerm;
                return;
            }
            const std::string localTitle = pairs.find(wiki, term, lang);
            const auto home = pairs.homeWiki.find(lang);
            const std::string homeTitle =
                    home != pairs.homeWiki.end() && home->second != wiki ? pairs.find(home->second, term, lang) : "";
            // en.wiktionary, of the most entries of other languages (sh, mk of de)
            const std::string enTitle =
                    wiki != "en" && (home == pairs.homeWiki.end() || home->second != "en") ? pairs.find("en", term, lang) : "";
            const bool local = !localTitle.empty(), inHome = !homeTitle.empty(), inEn = !enTitle.empty();
            if (local)
                t["page"] = true;
            if (inHome)
                t["home"] = true;
            if (inEn)
                t["en"] = true;
            // The page found under another title (without marks, of {{PAGENAME}}, of links)
            if (const auto &found = local ? localTitle : inHome ? homeTitle : enTitle;
                !found.empty() && found != t["term"].get<std::string>())
                t["page_title"] = found;
            c.page += local;
            c.home += inHome;
            c.en += inEn;
            c.both += local && inHome;
            c.none += !local && !inHome && !inEn;
        });
}

} // namespace pagelinks
