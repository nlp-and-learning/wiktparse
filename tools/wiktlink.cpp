// wiktlink: phase 2 on the output of wikt2jsonl (no dumps): the translations linked to pages.
// A translation (lang L, term T) of wiki W gets
//   "page": true  when W has a page T with a section of language L (pl.wikt "Wasser", de),
//   "home": true  when the Wiktionary of L has a page T with a section of L (de.wikt "Wasser"),
//   "page_title": the title found when it is not T (ru "кана́тная" -> "канатная", {{PAGENAME}}).
// Pass 1 reads every wiki's (title, section code) pairs as 64-bit hashes; pass 2 rewrites the
// pages with the links, in the same blocks (zstd with a dictionary per wiki, or bz2).
//
// wiktlink [-i jsonl_dump] [-o jsonl_link] [-j 16] [--bz2] [wiki...]
//   wiki...: the wikis to rewrite (all of the input by default); pass 1 reads all of them
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "jsonl_blocks.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using jsonlblocks::BlockReader;
using jsonlblocks::Compression;
using jsonlblocks::JsonlWriter;

namespace {

uint64_t key(const std::string &title, const std::string &code) {
    return std::hash<std::string>{}(title + '\x1f' + code);
}

// Codes of a wiki's own language (as nativeCodes of wikt2jsonl)
std::vector<std::string> nativeCodes(const std::string &wiki) {
    static const std::map<std::string, std::vector<std::string>> special = {
            {"simple", {}}, {"zh_min_nan", {"nan", "nan-hbl"}}, {"no", {"no", "nb"}}, {"roa_rup", {"rup"}}};
    if (auto it = special.find(wiki); it != special.end())
        return it->second;
    std::string code = wiki;
    std::ranges::replace(code, '_', '-');
    return {code};
}

// Runs f(worker, block) over the blocks of a reader in threads
void forBlocks(const BlockReader &reader, size_t threads, const std::function<void(size_t, size_t)> &f) {
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    std::exception_ptr error;
    std::mutex errorMutex;
    for (size_t t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            try {
                for (size_t b; (b = next++) < reader.size();)
                    f(t, b);
            } catch (...) {
                std::lock_guard lock(errorMutex);
                error = std::current_exception();
            }
        });
    for (auto &thread : pool)
        thread.join();
    if (error)
        std::rethrow_exception(error);
}

// Every translation of a section tree
void eachTranslation(json &node, const std::function<void(json &)> &f) {
    if (auto it = node.find("translations"); it != node.end())
        for (auto &t : *it)
            f(t);
    for (const char *key : {"sections", "children"})
        if (auto it = node.find(key); it != node.end())
            for (auto &child : *it)
                eachTranslation(child, f);
}

// The term as a page title: without an interwiki prefix (kn ":en:fire"), {{PAGENAME}} as the
// page's title (el), links as their text ("[[ristiä]] [[miekka|miekat]]" -> "ristiä miekat")
std::string termTitle(std::string term, const std::string &pageTitle) {
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
std::string withoutMarks(const std::string &text) {
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
const std::vector<std::string> &equivalents(const std::string &code) {
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

double seconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

} // namespace

int main(int argc, char *argv[]) {
    fs::path input = "jsonl_dump", output = "jsonl_link";
    size_t threads = 16;
    Compression compression = Compression::zstd;
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-i" && i + 1 < argc)
            input = argv[++i];
        else if (arg == "-o" && i + 1 < argc)
            output = argv[++i];
        else if (arg == "-j" && i + 1 < argc)
            threads = std::max(1, std::stoi(argv[++i]));
        else if (arg == "--bz2")
            compression = Compression::bz2;
        else if (arg == "-h" || arg == "--help") {
            std::cout << "usage: wiktlink [-i jsonl_dump] [-o jsonl_link] [-j 16] [--bz2] [wiki...]\n";
            return 0;
        } else
            only.push_back(arg);
    }
    try {
        const auto started = std::chrono::steady_clock::now();
        // The wikis of the input: <wiki>.jsonl.zst (or .bz2) with its index
        std::map<std::string, fs::path> files;
        for (const auto &entry : fs::directory_iterator(input)) {
            const std::string name = entry.path().filename().string();
            for (const char *ext : {".jsonl.zst", ".jsonl.bz2"})
                if (name.ends_with(ext) && !files.contains(name.substr(0, name.size() - strlen(ext))))
                    files[name.substr(0, name.size() - strlen(ext))] = entry.path();
        }
        if (files.empty())
            throw std::runtime_error("no <wiki>.jsonl.zst or .jsonl.bz2 in " + input.string());

        // Pass 1: (title, code) of every section of every wiki
        std::map<std::string, std::vector<uint64_t>> sections;
        for (const auto &[wiki, path] : files) {
            BlockReader reader(path);
            std::vector<std::vector<uint64_t>> found(threads);
            forBlocks(reader, threads, [&](size_t worker, size_t block) {
                for (const auto &line : reader.lines(block)) {
                    const json page = json::parse(line);
                    const std::string &title = page["title"].get_ref<const std::string &>();
                    for (const auto &s : page["sections"])
                        if (s.contains("code") && s["code"].is_string())
                            found[worker].push_back(key(title, s["code"].get<std::string>()));
                }
            });
            auto &all = sections[wiki];
            for (auto &f : found)
                all.insert(all.end(), f.begin(), f.end());
            std::ranges::sort(all);
            all.erase(std::unique(all.begin(), all.end()), all.end());
        }
        size_t pairs = 0;
        for (const auto &[wiki, keys] : sections)
            pairs += keys.size();
        std::cout << std::format("pass 1: {} wikis, {} (title, language) pairs, {:.0f} s", files.size(), pairs,
                                 seconds(started))
                  << std::endl;

        // The Wiktionary of each language
        std::map<std::string, std::string> homeWiki;
        for (const auto &[wiki, path] : files)
            for (const auto &code : nativeCodes(wiki))
                homeWiki.emplace(code, wiki);
        for (const auto &[code, wiki] : std::map<std::string, std::string>(homeWiki))
            for (const auto &other : equivalents(code))
                homeWiki.emplace(other, wiki); // cmn: zh.wiktionary
        auto has = [&](const std::string &wiki, const std::string &title, const std::string &code) {
            auto it = sections.find(wiki);
            return it != sections.end() && std::ranges::binary_search(it->second, key(title, code));
        };
        // The title of a page of the term with a section of the language (or an equivalent one),
        // exact or without marks; empty for none
        auto find = [&](const std::string &wiki, const std::string &term, const std::string &code) -> std::string {
            const std::string bare = withoutMarks(term);
            for (const std::string *title : {&term, &bare}) {
                if (title == &bare && bare == term)
                    break;
                if (has(wiki, *title, code))
                    return *title;
                for (const auto &other : equivalents(code))
                    if (has(wiki, *title, other))
                        return *title;
            }
            return {};
        };

        // Pass 2: the pages with their translations linked
        fs::create_directories(output);
        std::ofstream stats(output / "stats.tsv");
        stats << "wiki\ttranslations\tpage\thome\tboth\tnone\tno_lang\tno_term\n";
        for (const auto &[wiki, path] : files) {
            if (!only.empty() && !std::ranges::contains(only, wiki))
                continue;
            const auto wikiStarted = std::chrono::steady_clock::now();
            BlockReader reader(path);
            JsonlWriter out(output / wiki, compression, 9, threads, fs::file_size(path));
            struct Count {
                size_t translations = 0, page = 0, home = 0, both = 0, none = 0, noLang = 0, noTerm = 0;
            };
            std::vector<Count> counts(threads);
            forBlocks(reader, threads, [&](size_t worker, size_t block) {
                auto &c = counts[worker];
                for (const auto &line : reader.lines(block)) {
                    json page = json::parse(line);
                    const std::string title = page["title"].get<std::string>();
                    for (auto &s : page["sections"])
                        eachTranslation(s, [&](json &t) {
                            ++c.translations;
                            if (!t.contains("lang") || !t["lang"].is_string()) {
                                ++c.noLang;
                                return;
                            }
                            const std::string lang = t["lang"].get<std::string>();
                            const std::string term = termTitle(t["term"].get<std::string>(), title);
                            if (term.empty()) {
                                ++c.noTerm;
                                return;
                            }
                            const std::string localTitle = find(wiki, term, lang);
                            const auto home = homeWiki.find(lang);
                            const std::string homeTitle =
                                    home != homeWiki.end() && home->second != wiki ? find(home->second, term, lang) : "";
                            const bool local = !localTitle.empty(), inHome = !homeTitle.empty();
                            if (local)
                                t["page"] = true;
                            if (inHome)
                                t["home"] = true;
                            // The page found under another title (without marks, of {{PAGENAME}}, of links)
                            if (const auto &found = local ? localTitle : homeTitle; !found.empty() && found != t["term"].get<std::string>())
                                t["page_title"] = found;
                            c.page += local;
                            c.home += inHome;
                            c.both += local && inHome;
                            c.none += !local && !inHome;
                        });
                    out.add(worker, page.dump(-1, ' ', false, json::error_handler_t::replace),
                            page["title"].get<std::string>());
                }
            });
            out.close();
            Count total;
            for (const auto &c : counts) {
                total.translations += c.translations, total.page += c.page, total.home += c.home;
                total.both += c.both, total.none += c.none, total.noLang += c.noLang, total.noTerm += c.noTerm;
            }
            stats << std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", wiki, total.translations, total.page, total.home,
                                 total.both, total.none, total.noLang, total.noTerm);
            std::cout << std::format("{}: {} translations, page {}, home {}, none {} ({:.0f} s)", wiki,
                                     total.translations, total.page, total.home, total.none, seconds(wikiStarted))
                      << std::endl;
        }
        std::cout << std::format("done in {:.0f} s", seconds(started)) << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    return 0;
}
