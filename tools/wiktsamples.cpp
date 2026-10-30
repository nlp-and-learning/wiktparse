// Sample pages of every Wiktionary for designing the per-wiki configuration
// (HANDOFF-2, step 1): samples/<wiki>/ with 5 main namespace pages as raw
// wikitext and, when there is one, a page of another namespace.
//
// Pages are comparable between wikis: the words for water, dog, house, red
// and go, taken from the first translation table of these English Wiktionary
// entries; wiki codes become en.wikt codes through wd_code_map of
// wiktlangs.sqlite. Missing words are replaced by main namespace pages with
// the most headings among titles spread over the index. The page of another
// namespace is a translation subpage or thesaurus page of a word, otherwise
// the first page of a thesaurus, reconstruction or other content namespace
// (ids 100-199 in the dump's <siteinfo>).
//
// wiktsamples [-j N] [-n pages] [--other N] [-o samples] [--db wiktlangs.sqlite] [wiki...]
// wiktsamples --same-titles samples1000 -o samples_en [en]: the pages of en.wikt of the titles of the
// samples of the other wikis, to compare their entries (etymologies) with en.wikt
//
// Output: samples/<wiki>/NN <title>.wiki and samples/<wiki>/meta.json
// ([{"file","title","ns","reason","source"}], source: [bz2 block, number in it]),
// samples/<wiki>/pages.tsv (block, index, title per page). Only the indexed ranges holding wanted
// titles are read.
#include <algorithm>
#include <atomic>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <wikilib/core/unicode_utils.h>
#include <wikilib/dump/dump_reader.h>
#include <wikilib/dump/index_chunker.h>
#include <wikilib/templates/template_parser.h>

#include "dump/WikiDump.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace wikilib::dump;

namespace {

struct Concept {
    std::string word; // English Wiktionary entry
    std::string qid;  // Wikidata item of the meaning, if any
};

const std::vector<Concept> concepts = {
        {"water", "Q283"}, {"dog", "Q144"}, {"house", "Q3947"}, {"red", "Q3142"}, {"go", ""}};

size_t mainPages = 5; // -n
size_t otherPages = 1; // --other: pages outside the main namespace
size_t fallbackCandidates = 200; // titles spread over the index, at least 4 per page

// Translations of the first table: en.wikt code -> terms in order
using Translations = std::map<std::string, std::vector<std::string>>;

// Translation templates of the first {{trans-top}} ... {{trans-bottom}} in text
Translations firstTranslationTable(std::string_view text) {
    Translations result;
    bool inTable = false;
    // Templates in text order, with those in parameter values after their template
    // ({{multitrans|data=* Polish: {{tt+|pl|czerwony}}...}})
    std::vector<wikilib::templates::TemplateInvocation> invocations;
    std::function<void(std::string_view)> collect = [&](std::string_view part) {
        for (auto &invocation : wikilib::templates::find_invocations(part)) {
            auto parameters = invocation.parameters;
            invocations.push_back(std::move(invocation));
            for (const auto &[name, value] : parameters)
                if (value.find("{{") != std::string::npos)
                    collect(value);
        }
    };
    collect(text);
    for (const auto &invocation : invocations) {
        const auto &name = invocation.name;
        if (name == "trans-top" || name == "trans-top-see" || name == "trans-top-also" || name == "checktrans-top") {
            if (inTable || !result.empty())
                break;
            inTable = true;
        } else if (name == "trans-bottom") {
            if (inTable)
                break;
        } else if (inTable && (name == "t" || name == "t+" || name == "tt" || name == "tt+" || name == "t-check" ||
                               name == "t+check")) {
            auto code = invocation.get(size_t{1});
            auto term = invocation.get(size_t{2});
            if (code && term && !term->empty())
                result[std::string(*code)].emplace_back(*term);
        }
    }
    return result;
}

// English section of an entry (from ==English== to the next level 2 header)
std::string_view englishSection(std::string_view text) {
    auto start = text.find("==English==");
    if (start == std::string_view::npos)
        return {};
    auto end = text.find("\n==", start + 11);
    while (end != std::string_view::npos && end + 3 < text.size() && text[end + 3] == '=')
        end = text.find("\n==", end + 3);
    return text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
}

// Translations of every concept from English Wiktionary (entry or its /translations subpage)
std::vector<Translations> conceptTranslations() {
    std::set<std::string> titles;
    for (const auto &c : concepts) {
        titles.insert(c.word);
        titles.insert(c.word + "/translations");
    }
    auto pages = WikiDump::wiktionary("en").extractMatching([&](const std::string &t) { return titles.contains(t); });
    std::vector<Translations> result;
    for (const auto &c : concepts) {
        // The subpage holds the main sense ({{see translation subpage|Noun}} on the entry)
        auto table = firstTranslationTable(pages[c.word + "/translations"]);
        if (table.empty())
            table = firstTranslationTable(englishSection(pages[c.word]));
        std::cout << "en " << c.word << ": translations into " << table.size() << " languages" << std::endl;
        result.push_back(std::move(table));
    }
    return result;
}

// en.wikt codes of a wiki: from code_map (Wikimedia codes), then the code itself;
// translation tables use specific languages for some macrolanguages
std::vector<std::string> wiktCodes(sqlite3 *db, const std::string &wiki) {
    if (wiki == "en" || wiki == "simple")
        return {"en"};
    std::string code = wiki;
    std::replace(code.begin(), code.end(), '_', '-');
    std::vector<std::string> codes;
    if (db) {
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT language FROM code_map WHERE scheme='wikimedia' AND code=?",
                           -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW)
            codes.emplace_back(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        sqlite3_finalize(stmt);
    }
    codes.push_back(code);
    static const std::map<std::string, std::vector<std::string>> specific = {
            {"zh", {"cmn"}}, {"no", {"nb"}}, {"nan", {"nan-hbl"}}, {"ku", {"kmr"}}, {"hr", {"sh"}},
            {"sr", {"sh"}}, {"bs", {"sh"}}, {"ms", {"zsm"}}, {"fa", {"fa-ira", "prs"}}, {"ar", {"arb"}},
            {"et", {"ekk"}}, {"lv", {"lvs"}}, {"sw", {"swh"}}};
    for (const auto &c : std::vector<std::string>(codes))
        if (auto it = specific.find(c); it != specific.end())
            codes.insert(codes.end(), it->second.begin(), it->second.end());
    std::vector<std::string> unique;
    for (const auto &c : codes)
        if (std::ranges::find(unique, c) == unique.end())
            unique.push_back(c);
    return unique;
}

bool isCombining(char32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0591 && c <= 0x05C7) /* Hebrew points */ ||
           (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) || c == 0x0670 /* Arabic harakat */ ||
           (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F);
}

std::string withoutCombining(std::string_view text) {
    std::string result;
    for (size_t pos = 0; pos < text.size();) {
        size_t start = pos;
        auto c = wikilib::unicode::decode_utf8(text, pos);
        if (!c) {
            pos = start + 1;
            continue;
        }
        if (!isCombining(*c))
            result += text.substr(start, pos - start);
    }
    return result;
}

// Title variants: as written, with the first letter lower and upper case; then
// without stress marks (вода́ -> вода) and without all diacritics (ā -> a), as
// en.wikt writes translations with marks that entry titles do not have
std::vector<std::string> titleVariants(const std::string &term) {
    std::vector<std::string> variants;
    auto add = [&](const std::string &title) {
        size_t pos = 0;
        if (!wikilib::unicode::decode_utf8(title, pos))
            return;
        std::string head = title.substr(0, pos);
        std::string rest = title.substr(pos);
        for (const auto &variant : {title, wikilib::unicode::to_lower(head) + rest, wikilib::unicode::to_upper(head) + rest})
            if (std::ranges::find(variants, variant) == variants.end())
                variants.push_back(variant);
    };
    add(term);
    add(withoutCombining(term));
    add(wikilib::unicode::normalize(withoutCombining(wikilib::unicode::normalize(term, wikilib::unicode::NormalizationForm::NFD)),
                                    wikilib::unicode::NormalizationForm::NFC));
    return variants;
}

size_t headingCount(std::string_view text) {
    size_t count = text.starts_with("=") ? 1 : 0;
    for (size_t pos = 0; (pos = text.find("\n=", pos)) != std::string_view::npos; ++pos)
        ++count;
    return count;
}

// Namespaces of the wiki with entries besides the main one (ids 100-199, not
// talk), as "Name:" prefixes: thesaurus first, then reconstructions, then by id
struct SpecialNamespaces {
    std::vector<std::string> thesaurus;
    std::vector<std::string> all; // in the order of preference
};

SpecialNamespaces specialNamespaces(const wikilib::dump::SiteInfo &site) {
    auto contains = [](const std::string &name, std::initializer_list<std::string_view> words) {
        auto lower = wikilib::unicode::to_lower(name);
        return std::ranges::any_of(words, [&](auto word) { return lower.find(word) != std::string::npos; });
    };
    SpecialNamespaces result;
    std::vector<std::string> reconstruction, other;
    for (const auto &ns : site.namespaces) {
        if (ns.id < 100 || ns.id > 199 || ns.is_talk || ns.name.empty())
            continue;
        // Portals are navigation, not entries
        if (ns.name == "Portal" || ns.name == "Portail" || ns.name == "Портал" || ns.name == "Portale")
            continue;
        std::string prefix = ns.name + ":";
        if (contains(ns.name, {"saurus", "тезаурус"}))
            result.thesaurus.push_back(prefix);
        else if (contains(ns.name, {"econstr", "ekonstr", "еконстр"}))
            reconstruction.push_back(prefix);
        else
            other.push_back(prefix);
    }
    result.all = result.thesaurus;
    result.all.insert(result.all.end(), reconstruction.begin(), reconstruction.end());
    result.all.insert(result.all.end(), other.begin(), other.end());
    return result;
}

std::string fileName(size_t n, const std::string &title) {
    std::string name;
    for (char c : title)
        name += c == '/' ? std::string("%2F") : std::string(1, c);
    if (name.starts_with("."))
        name = "%2E" + name.substr(1);
    if (name.size() > 200)
        name.resize(200);
    return std::format("{:02} {}.wiki", n, name);
}

struct Chosen {
    std::string title;
    int ns;
    std::string reason;
    std::string text;
    uint64_t block = 0; // start of the page's bz2 block in the dump
    size_t index = 0;   // number of the page in that block, from 0
};

std::vector<Chosen> samplesOf(const std::string &wiki, const std::vector<std::string> &codes,
                              const std::vector<Translations> &translations) {
    auto path = WikiDump::dumpPath(WikiProject::Wiktionary, wiki);
    // Concept titles in the order of preference
    std::vector<std::vector<std::string>> conceptTitles(concepts.size());
    std::set<std::string> wanted;
    for (size_t i = 0; i < concepts.size(); ++i) {
        if (codes == std::vector<std::string>{"en"}) {
            conceptTitles[i].push_back(concepts[i].word);
        } else {
            for (const auto &code : codes)
                if (auto it = translations[i].find(code); it != translations[i].end())
                    for (const auto &term : it->second)
                        for (const auto &variant : titleVariants(term))
                            if (std::ranges::find(conceptTitles[i], variant) == conceptTitles[i].end())
                                conceptTitles[i].push_back(variant);
        }
        wanted.insert(conceptTitles[i].begin(), conceptTitles[i].end());
    }
    // Fallback titles spread over the index, and pages of other namespaces
    DumpReader reader(path);
    auto site = reader.read_site_info();
    if (!site)
        throw std::runtime_error(wiki + ": " + reader.error());
    const auto specialNs = specialNamespaces(*site);
    const auto &thesaurusPrefixes = specialNs.thesaurus;
    std::vector<std::string> allTitles; // main namespace
    std::map<std::string, std::vector<std::string>> firstByPrefix; // a few, the first may be a redirect
    {
        auto chunker = IndexChunker::from_file(path.index_path().string(), 0);
        for (IndexChunk chunk; chunker.next_chunk(chunk);)
            for (auto &entry : chunk.entries) {
                if (site->namespace_of(entry.title) == 0) {
                    allTitles.push_back(std::move(entry.title));
                    continue;
                }
                for (const auto &prefix : specialNs.all)
                    if (entry.title.starts_with(prefix) && firstByPrefix[prefix].size() < 20)
                        firstByPrefix[prefix].push_back(entry.title);
            }
        if (!chunker.error().empty())
            throw std::runtime_error(wiki + ": " + std::string(chunker.error()));
    }
    std::set<std::string> fallback;
    const size_t step = std::max<size_t>(1, allTitles.size() / fallbackCandidates);
    for (size_t i = step / 2; i < allTitles.size(); i += step)
        fallback.insert(allTitles[i]);
    allTitles.clear();
    for (const auto &[prefix, titles] : firstByPrefix)
        for (const auto &title : titles)
            wanted.insert(title);
    // Translation subpages and thesaurus pages of concept titles (en: water/translations, Thesaurus:dog)
    for (const auto &titles : conceptTitles)
        for (const auto &t : titles) {
            wanted.insert(t + "/translations");
            for (const auto &prefix : thesaurusPrefixes)
                wanted.insert(prefix + t);
        }

    std::map<std::string, Chosen> pages;
    bool ok = reader.process_matching(
            [&](const std::string &t) { return wanted.contains(t) || fallback.contains(t); },
            [&](const Page &page) {
                if (!page.info.is_redirect())
                    pages[page.info.title] = {page.info.title, static_cast<int>(page.info.namespace_id), "",
                                              std::string(page.content())};
                return true;
            });
    if (!ok)
        throw std::runtime_error(wiki + ": " + reader.error());
    // Where the pages are: their bz2 block and number in it, for reading them without the index
    {
        auto chunker = IndexChunker::from_file(path.index_path().string(), 0);
        for (IndexChunk chunk; chunker.next_chunk(chunk);)
            for (size_t i = 0; i < chunk.entries.size(); ++i)
                if (auto it = pages.find(chunk.entries[i].title); it != pages.end()) {
                    it->second.block = chunk.start_offset;
                    it->second.index = i;
                }
        if (!chunker.error().empty())
            throw std::runtime_error(wiki + ": " + std::string(chunker.error()));
    }

    std::vector<Chosen> chosen;
    std::set<std::string> used;
    for (size_t i = 0; i < concepts.size(); ++i)
        for (const auto &t : conceptTitles[i]) {
            auto it = pages.find(t);
            if (it == pages.end() || it->second.ns != 0 || used.contains(t))
                continue;
            Chosen c = it->second;
            c.reason = "concept " + concepts[i].word + (concepts[i].qid.empty() ? "" : " (" + concepts[i].qid + ")");
            chosen.push_back(std::move(c));
            used.insert(t);
            break;
        }
    std::vector<const Chosen *> others;
    for (const auto &[title, page] : pages)
        if (fallback.contains(title) && page.ns == 0 && !used.contains(title))
            others.push_back(&page);
    // A few pages for reading: the richest ones; many pages for testing: spread
    // over the index, any page with a heading or template line
    const bool spread = mainPages > 10;
    if (spread) {
        // Pages with a heading or a template line (nl, it mark sections with {{-xx-}} lines)
        std::erase_if(others, [](const Chosen *c) { return headingCount(c->text) == 0 && c->text.find("\n{{") == std::string::npos; });
        const size_t want = mainPages - std::min(mainPages, chosen.size());
        if (others.size() > want) {
            std::vector<const Chosen *> picked;
            for (size_t i = 0; i < want; ++i)
                picked.push_back(others[i * others.size() / want]);
            others = std::move(picked);
        }
    } else {
        std::ranges::sort(others, [](const Chosen *a, const Chosen *b) {
            auto ha = headingCount(a->text), hb = headingCount(b->text);
            return ha != hb ? ha > hb : a->title < b->title;
        });
    }
    for (const auto *page : others) {
        if (chosen.size() >= mainPages)
            break;
        Chosen c = *page;
        c.reason = spread ? "spread over the index"
                          : "most headings (" + std::to_string(headingCount(c.text)) + ") among sampled titles";
        used.insert(c.title);
        chosen.push_back(std::move(c));
    }

    // One page outside the main namespace: translation subpage or thesaurus of a
    // concept, otherwise the first page of a thesaurus, reconstruction or appendix
    std::vector<std::pair<std::string, std::string>> specials; // title, reason
    for (const auto &c : chosen)
        if (c.reason.starts_with("concept")) {
            specials.emplace_back(c.title + "/translations", "translation subpage of " + c.title);
            for (const auto &prefix : thesaurusPrefixes)
                specials.emplace_back(prefix + c.title, "thesaurus page of " + c.title);
        }
    // Pages of the namespaces in turn, so that several namespaces are shown
    for (size_t round = 0; round < 10; ++round)
        for (const auto &prefix : specialNs.all)
            if (auto it = firstByPrefix.find(prefix); it != firstByPrefix.end() && round < it->second.size())
                specials.emplace_back(it->second[round], "page of " + prefix);
    size_t taken = 0;
    for (const auto &[title, reason] : specials) {
        if (taken >= otherPages)
            break;
        if (auto it = pages.find(title); it != pages.end() && !used.contains(title)) {
            Chosen c = it->second;
            c.reason = reason;
            used.insert(title);
            chosen.push_back(std::move(c));
            ++taken;
        }
    }
    return chosen;
}

void writeSamples(const fs::path &dir, const std::vector<Chosen> &chosen) {
    fs::remove_all(dir);
    fs::create_directories(dir);
    json meta = json::array();
    // pages.tsv: one line per page, its bz2 block and number in it: enough to read it from the dump
    std::ofstream positions(dir / "pages.tsv");
    positions << "block\tindex\ttitle\n";
    for (size_t i = 0; i < chosen.size(); ++i) {
        const auto &c = chosen[i];
        auto file = fileName(i + 1, c.title);
        std::ofstream(dir / file) << c.text;
        json page = {{"file", file}, {"title", c.title}, {"ns", c.ns}, {"reason", c.reason}};
        if (c.block) // (pages of --same-titles: of no known block)
            page["source"] = {c.block, c.index};
        meta.push_back(std::move(page));
        if (c.block)
            positions << c.block << '\t' << c.index << '\t' << c.title << '\n';
    }
    std::ofstream(dir / "meta.json") << meta.dump(1, '\t', false, json::error_handler_t::replace) << '\n';
}

// --same-titles: the pages of a wiki of the titles of the main namespace pages of the samples of the
// other wikis (samples1000/*/meta.json), to compare an entry of two wikis (pl.wikt woda and the Polish
// section of en.wikt woda)
std::vector<Chosen> sameTitles(const std::string &wiki, const fs::path &samples) {
    std::map<std::string, std::string> wanted; // title -> the first wiki of it
    for (const auto &entry : fs::directory_iterator(samples)) {
        const auto other = entry.path().filename().string();
        if (other == wiki || !fs::exists(entry.path() / "meta.json"))
            continue;
        std::ifstream in(entry.path() / "meta.json");
        for (const auto &page : json::parse(in))
            if (page.value("ns", 0) == 0)
                wanted.emplace(page["title"].get<std::string>(), other);
    }
    auto pages = WikiDump::wiktionary(wiki).extractMatching([&](const std::string &t) { return wanted.contains(t); });
    std::vector<Chosen> chosen;
    for (auto &[title, text] : pages)
        chosen.push_back({title, 0, "title of the " + wanted[title] + " sample", std::move(text)});
    return chosen;
}

} // namespace

int main(int argc, char *argv[]) {
    try {
        size_t threads = 8;
        fs::path output = "samples";
        fs::path dbFile = "wiktlangs.sqlite";
        fs::path sameTitlesOf;
        std::vector<std::string> wikis;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-j" && i + 1 < argc)
                threads = std::max(1, std::stoi(argv[++i]));
            else if (arg == "--other" && i + 1 < argc)
                otherPages = std::stoul(argv[++i]);
            else if (arg == "-n" && i + 1 < argc) {
                mainPages = std::max(1, std::stoi(argv[++i]));
                fallbackCandidates = std::max<size_t>(200, mainPages * 4);
            } else if (arg == "-o" && i + 1 < argc)
                output = argv[++i];
            else if (arg == "--db" && i + 1 < argc)
                dbFile = argv[++i];
            else if (arg == "--same-titles" && i + 1 < argc)
                sameTitlesOf = argv[++i];
            else if (arg == "-h" || arg == "--help") {
                std::cout << "usage: wiktsamples [-j N] [-n pages] [--other N] [-o samples] [--db wiktlangs.sqlite] [wiki...]\n"
                             "       wiktsamples --same-titles samples1000 -o samples_en [en]\n";
                return 0;
            } else
                wikis.push_back(arg);
        }
        if (!sameTitlesOf.empty()) {
            for (const auto &wiki : wikis.empty() ? std::vector<std::string>{"en"} : wikis) {
                auto chosen = sameTitles(wiki, sameTitlesOf);
                writeSamples(output / wiki, chosen);
                std::cout << wiki << ": " << chosen.size() << " pages of the titles of " << sameTitlesOf.string()
                          << " -> " << (output / wiki).string() << std::endl;
            }
            return 0;
        }
        if (wikis.empty())
            wikis = WikiDump::dumpPath(WikiProject::Wiktionary, "en").available_languages();

        sqlite3 *db = nullptr;
        if (!fs::exists(dbFile) || sqlite3_open_v2(dbFile.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            std::cerr << "warning: no " << dbFile << ", wiki codes are used as en.wikt codes" << std::endl;
            sqlite3_close(db);
            db = nullptr;
        }
        std::map<std::string, std::vector<std::string>> codes;
        for (const auto &wiki : wikis)
            codes[wiki] = wiktCodes(db, wiki);
        sqlite3_close(db);

        auto translations = conceptTranslations();

        std::mutex outMutex;
        std::atomic<size_t> next{0};
        std::atomic<size_t> failed{0};
        auto work = [&] {
            for (size_t i; (i = next++) < wikis.size();) {
                const auto &wiki = wikis[i];
                try {
                    auto chosen = samplesOf(wiki, codes[wiki], translations);
                    writeSamples(output / wiki, chosen);
                    size_t byConcept = std::ranges::count_if(chosen, [](const Chosen &c) { return c.reason.starts_with("concept"); });
                    std::lock_guard lock(outMutex);
                    std::cout << wiki << ": " << chosen.size() << " pages, " << byConcept << " by concept" << std::endl;
                } catch (const std::exception &e) {
                    failed++;
                    std::lock_guard lock(outMutex);
                    std::cerr << wiki << ": " << e.what() << std::endl;
                }
            }
        };
        std::vector<std::thread> pool;
        for (size_t t = 0; t < std::min(threads, wikis.size()); ++t)
            pool.emplace_back(work);
        for (auto &thread : pool)
            thread.join();
        std::cout << wikis.size() - failed << " wikis written to " << output.string() << std::endl;
        return failed ? 1 : 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
