// wiktlink: phase 2 on the output of wikt2jsonl (no dumps): the translations linked to pages.
// A translation (lang L, term T) of wiki W gets
//   "page": true  when W has a page T with a section of language L (pl.wikt "Wasser", de),
//   "home": true  when the Wiktionary of L has a page T with a section of L (de.wikt "Wasser").
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
        auto has = [&](const std::string &wiki, const std::string &title, const std::string &code) {
            auto it = sections.find(wiki);
            return it != sections.end() && std::ranges::binary_search(it->second, key(title, code));
        };

        // Pass 2: the pages with their translations linked
        fs::create_directories(output);
        std::ofstream stats(output / "stats.tsv");
        stats << "wiki\ttranslations\tpage\thome\tboth\tnone\tno_lang\n";
        for (const auto &[wiki, path] : files) {
            if (!only.empty() && !std::ranges::contains(only, wiki))
                continue;
            const auto wikiStarted = std::chrono::steady_clock::now();
            BlockReader reader(path);
            JsonlWriter out(output / wiki, compression, 9, threads, fs::file_size(path));
            struct Count {
                size_t translations = 0, page = 0, home = 0, both = 0, none = 0, noLang = 0;
            };
            std::vector<Count> counts(threads);
            forBlocks(reader, threads, [&](size_t worker, size_t block) {
                auto &c = counts[worker];
                for (const auto &line : reader.lines(block)) {
                    json page = json::parse(line);
                    for (auto &s : page["sections"])
                        eachTranslation(s, [&](json &t) {
                            ++c.translations;
                            if (!t.contains("lang") || !t["lang"].is_string()) {
                                ++c.noLang;
                                return;
                            }
                            const std::string lang = t["lang"].get<std::string>();
                            const std::string &term = t["term"].get_ref<const std::string &>();
                            const bool local = has(wiki, term, lang);
                            const auto home = homeWiki.find(lang);
                            const bool inHome = home != homeWiki.end() && home->second != wiki && has(home->second, term, lang);
                            if (local)
                                t["page"] = true;
                            if (inHome)
                                t["home"] = true;
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
                total.both += c.both, total.none += c.none, total.noLang += c.noLang;
            }
            stats << std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\n", wiki, total.translations, total.page, total.home,
                                 total.both, total.none, total.noLang);
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
