// wiktlink: phase 2 on the output of wikt2jsonl (no dumps): the translations linked to pages.
// A translation (lang L, term T) of wiki W gets
//   "page": true  when W has a page T with a section of language L (pl.wikt "Wasser", de),
//   "home": true  when the Wiktionary of L has a page T with a section of L (de.wikt "Wasser"),
//   "en": true    when en.wiktionary (of the most entries of other languages) has one,
//   "page_title": the title found when it is not T (ru "кана́тная" -> "канатная", {{PAGENAME}});
// a reference of a translation section to another page (wikt2jsonl "see": en {{trans-see}})
// gets "page": true when that page has a section of the language.
// Pass 1 reads every wiki's (title, section code) pairs as 64-bit hashes; pass 2 rewrites the
// pages with the links, in the same blocks (zstd with a dictionary per wiki, or bz2).
// Translation subpages (en water/translations) go into their base page. The uniform tree
// (wikt2jsonl --uniform) the same way: its languages, the translations of its entries; the
// translations of an entry of the subpage go into the entry of the same part of speech.
//
// wiktlink [-i jsonl_dump] [-o jsonl_link] [-j 16] [--bz2] [--force] [wiki...]
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
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "jsonl_blocks.h"
#include "pagelinks.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using jsonlblocks::BlockReader;
using jsonlblocks::Compression;
using jsonlblocks::JsonlWriter;

namespace {

using namespace pagelinks;

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

// The parts of speech of a section tree with their Translations child, in page order
void partsOfSpeech(json &node, std::vector<json *> &out) {
    for (const char *k : {"sections", "children"})
        if (auto it = node.find(k); it != node.end())
            for (auto &child : *it) {
                if (child.contains("translations") || child.value("section", "") == "Translations")
                    continue;
                bool hasTranslations = false;
                if (auto c = child.find("children"); c != child.end())
                    for (const auto &g : *c)
                        hasTranslations |= g.value("section", "") == "Translations";
                if (hasTranslations)
                    out.push_back(&child);
                partsOfSpeech(child, out);
            }
}

// The Translations of a subpage into its base page: for each language and part of speech of the
// subpage, the first part of speech of the same name of that language on the page without
// translations of its own gets the subpage's Translations node; the number merged
size_t mergeSubpage(json &page, json subpage) {
    size_t merged = 0;
    for (auto &subLanguage : subpage["sections"]) {
        std::vector<json *> from;
        partsOfSpeech(subLanguage, from);
        for (auto &language : page["sections"]) {
            if (language.value("code", json()) != subLanguage.value("code", json()))
                continue;
            for (json *pos : from) {
                json translations;
                for (const auto &g : (*pos)["children"])
                    if (g.value("section", "") == "Translations")
                        translations = g;
                // The target: a part of speech of that name without a Translations child
                std::function<json *(json &)> target = [&](json &node) -> json * {
                    for (const char *k : {"sections", "children"})
                        if (auto it = node.find(k); it != node.end())
                            for (auto &child : *it) {
                                if (child.value("section", "") == pos->value("section", "")) {
                                    bool has = false;
                                    if (auto c = child.find("children"); c != child.end())
                                        for (const auto &g : *c)
                                            has |= g.value("section", "") == "Translations";
                                    if (!has)
                                        return &child;
                                }
                                if (json *found = target(child))
                                    return found;
                            }
                    return nullptr;
                };
                translations["subpage"] = true;
                // Where it is in the dump: the subpage's own place, not the page's
                if (subpage.contains("source"))
                    translations["source"] = subpage["source"];
                translations["source_title"] = subpage.value("title", "");
                if (json *to = target(language))
                    (*to)["children"].push_back(std::move(translations));
                else // no such part of speech: under the language, not lost
                    language["sections"].push_back(std::move(translations));
                ++merged;
            }
        }
    }
    return merged;
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
    bool force = false;
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
        else if (arg == "--force")
            force = true;
        else if (arg == "-h" || arg == "--help") {
            std::cout << "usage: wiktlink [-i jsonl_dump] [-o jsonl_link] [-j 16] [--bz2] [--force] [wiki...]\n"
                         "       input: the tree of headings or the uniform tree (wikt2jsonl --uniform)\n"
                         "       a wiki whose output is complete (its .idx ends with EOF=) and newer than its input is\n"
                         "       left as it is; --force: all again\n";
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
        Pairs pairs;
        // Translation subpages by wiki and base title (only the rewritten wikis)
        std::map<std::string, std::map<std::string, std::string>> subpages;
        std::mutex subpageMutex;
        for (const auto &[wiki, path] : files) {
            BlockReader reader(path);
            std::vector<std::vector<uint64_t>> found(threads);
            const bool rewritten = only.empty() || std::ranges::contains(only, wiki);
            forBlocks(reader, threads, [&](size_t worker, size_t block) {
                for (const auto &line : reader.lines(block)) {
                    const json page = json::parse(line);
                    const std::string &title = page["title"].get_ref<const std::string &>();
                    if (auto base = subpageBase(title); rewritten && !base.empty()) {
                        std::lock_guard lock(subpageMutex);
                        subpages[wiki][base] = line;
                    }
                    for (const auto &s : languagesOf(page))
                        if (s.contains("code") && s["code"].is_string())
                            found[worker].push_back(key(title, s["code"].get<std::string>()));
                }
            });
            std::vector<uint64_t> all;
            for (auto &f : found)
                all.insert(all.end(), f.begin(), f.end());
            pairs.add(wiki, std::move(all));
            std::cout << std::format("pass 1: {} {} pairs ({:.0f} s)", wiki, pairs.sections[wiki].size(), seconds(started))
                      << std::endl;
        }
        pairs.finish();
        std::cout << std::format("pass 1: {} wikis, {} (title, language) pairs, {:.0f} s", files.size(), pairs.size(),
                                 seconds(started))
                  << std::endl;

        // Pass 2: the pages with their translations linked
        fs::create_directories(output);
        // The rows of stats.tsv of a run before, by wiki: of the wikis left as they are
        std::map<std::string, std::string> statsBefore;
        {
            std::ifstream in(output / "stats.tsv");
            for (std::string line; std::getline(in, line);)
                if (auto tab = line.find('\t'); tab != std::string::npos && !line.starts_with("wiki\t"))
                    statsBefore[line.substr(0, tab)] = line;
        }
        std::ofstream stats(output / "stats.tsv");
        stats << "wiki\ttranslations\tpage\thome\ten\tboth\tnone\tno_lang\tno_term\tsubpages\tsee\tsee_found\n";
        for (const auto &[wiki, path] : files) {
            if (!only.empty() && !std::ranges::contains(only, wiki))
                continue;
            // (a wiki done before, not older than its input)
            if (!force && jsonlblocks::complete(output, wiki, path)) {
                if (statsBefore.contains(wiki))
                    stats << statsBefore[wiki] << '\n';
                std::cout << wiki << ": done before, left as it is (--force: again)" << std::endl;
                continue;
            }
            const auto wikiStarted = std::chrono::steady_clock::now();
            BlockReader reader(path);
            JsonlWriter out(output / wiki, compression, 9, threads, fs::file_size(path));
            // Translation subpages of this wiki, and the base pages they went into
            const auto subIt = subpages.find(wiki);
            const std::map<std::string, std::string> *wikiSubpages = subIt == subpages.end() ? nullptr : &subIt->second;
            std::vector<LinkCount> counts(threads);
            forBlocks(reader, threads, [&](size_t worker, size_t block) {
                auto &c = counts[worker];
                for (const auto &line : reader.lines(block)) {
                    json page = json::parse(line);
                    const std::string title = page["title"].get<std::string>();
                    // A subpage goes into its base page (of a section of its language), not on its own
                    if (auto base = subpageBase(title); !base.empty()) {
                        bool intoBase = false;
                        for (const auto &s : languagesOf(page))
                            intoBase |= s.contains("code") && s["code"].is_string() &&
                                        pairs.has(wiki, base, s["code"].get<std::string>());
                        if (intoBase)
                            continue;
                    }
                    if (wikiSubpages)
                        if (auto it = wikiSubpages->find(title); it != wikiSubpages->end())
                            c.subpages += (page.contains("languages") ? mergeUniformSubpage(page, json::parse(it->second))
                                                                      : mergeSubpage(page, json::parse(it->second))) > 0;
                    linkPage(page, wiki, pairs, c);
                    out.add(worker, page.dump(-1, ' ', false, json::error_handler_t::replace),
                            page["title"].get<std::string>());
                }
            });
            out.close();
            LinkCount total;
            for (const auto &c : counts)
                total.add(c);
            stats << std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", wiki, total.translations, total.page,
                                 total.home, total.en, total.both, total.none, total.noLang, total.noTerm, total.subpages, total.see,
                                 total.seeFound);
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
