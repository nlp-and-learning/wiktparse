// wiktglos: phase 2 on the output of wiktlink (or wikt2jsonl): the glosses of translation
// tables linked to the definitions (HANDOFF-3). For each part of speech whose translations
// have a table gloss and no sense number (en {{trans-top|domestic species}}; not pl, de, fr
// with numbers), the definitions (senses, wikitext reduced to words) and the distinct glosses
// go to defglos::match (positional alignment, then the best of the rest); a translation of a
// linked gloss gets "sense_ids" (["1", "1.2"]), "sense_method" (positional, best; number for a
// gloss that is a sense number, ja "語義1"; single for the one definition of an entry) and
// "sense_score"; --extra: links by embeddings (glossembed.py --apply) for the glosses left
// ("embedding"; "llm" of glossllm-*.tsv; "minority" of *.minority.tsv: the one model of three
// that gave a definition, so that a gloss with translations is not left unlinked). Input: wiktlink output (deterministic again: the TSV is of its entries).
// The uniform tree (wikt2jsonl --uniform) the same way, entry by entry: the senses of an entry and
// the glosses of its translations; its TSV keyed by part of speech instead of the path of a node of
// the tree of headings (glossrekey.py).
//
// wiktglos [-i jsonl_link] [-o jsonl_senses] [-j 16] [--extra file.tsv]... [wiki...]
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
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <wikilib/templates/template_parser.h>

#include "defglos/Matcher.h"
#include "glosslink.h"
#include "jsonl_blocks.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using jsonlblocks::BlockReader;
using jsonlblocks::Compression;
using jsonlblocks::JsonlWriter;

namespace {

using namespace glosslink;

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

// The senses of a part of speech: its own, and those of its Definitions children (pl-like)
void collectSenses(const json &node, std::vector<defglos::Item> &defs) {
    if (auto it = node.find("senses"); it != node.end())
        for (const auto &s : *it)
            defs.push_back({field(s, "id"), plainText(field(s, "text"))});
    // (and of a part of speech of the same name below it: en {{en-verb}} under ===Verb=== of
    // wikt2jsonl before 2026-10-25)
    if (auto it = node.find("children"); it != node.end())
        for (const auto &c : *it)
            if (field(c, "section") == "Definitions" ||
                (field(c, "section") == field(node, "section") && c.contains("senses")))
                collectSenses(c, defs);
}

// A part of speech with senses and Translations children of the tree of headings: its glosses linked
// to its senses (path: of the node in its language section, as glossembed.py writes it)
void linkNode(json &node, const std::string &title, Count &count, const std::string &language,
              const std::string &path, const Extras &extras) {
    for (const char *key : {"sections", "children"})
        if (auto it = node.find(key); it != node.end())
            for (size_t i = 0; i < it->size(); ++i)
                linkNode((*it)[i], title, count, language,
                         (path.empty() ? "" : path + "/") + key + ":" + std::to_string(i), extras);
    if (!node.contains("senses") || !node.contains("children"))
        return;
    std::vector<json *> tables;
    for (auto &c : node["children"])
        if (field(c, "section") == "Translations" && c.contains("translations"))
            tables.push_back(&c["translations"]);
    if (tables.empty())
        return;
    defglos::Entry entry;
    entry.title = title;
    entry.pos = field(node, "section");
    collectSenses(node, entry.defs);
    linkEntry(entry, tables, count, [&](const std::string &gloss) -> const Extra * {
        auto it = extras.byPath.find(extraKey(title, language, path, gloss));
        return it == extras.byPath.end() ? nullptr : &it->second;
    });
}

double seconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

} // namespace

int main(int argc, char *argv[]) {
    fs::path input = "jsonl_link", output = "jsonl_senses";
    size_t threads = 16;
    std::vector<std::string> only;
    std::vector<fs::path> extraFiles;
    bool force = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-i" && i + 1 < argc)
            input = argv[++i];
        else if (arg == "-o" && i + 1 < argc)
            output = argv[++i];
        else if (arg == "-j" && i + 1 < argc)
            threads = std::max(1, std::stoi(argv[++i]));
        else if (arg == "--extra" && i + 1 < argc)
            extraFiles.emplace_back(argv[++i]);
        else if (arg == "--force")
            force = true;
        else if (arg == "-h" || arg == "--help") {
            std::cout << "usage: wiktglos [-i jsonl_link] [-o jsonl_senses] [-j 16] [--extra glossembed-en.tsv]... [--force] [wiki...]\n"
                         "       input: the tree of headings or the uniform tree (wikt2jsonl --uniform)\n"
                         "       --extra: links by embeddings or LLMs (glossembed.py --apply, glossllm.py), the accepted\n"
                         "       ones; for the uniform tree their TSV by part of speech (glossrekey.py)\n"
                         "       a wiki whose output is complete (its .idx ends with EOF=) and newer than its input is\n"
                         "       left as it is; --force: all again (after a change of the --extra files)\n";
            return 0;
        } else
            only.push_back(arg);
    }
    try {
        const auto started = std::chrono::steady_clock::now();
        const Extras extras = readExtras(extraFiles);
        if (!extraFiles.empty())
            std::cout << std::format("{} links by embeddings and LLMs", extras.size()) << std::endl;
        std::map<std::string, fs::path> files;
        for (const auto &entry : fs::directory_iterator(input)) {
            const std::string name = entry.path().filename().string();
            if (name.ends_with(".jsonl.zst"))
                files[name.substr(0, name.size() - 10)] = entry.path();
        }
        if (files.empty())
            throw std::runtime_error("no <wiki>.jsonl.zst in " + input.string());
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
        stats << "wiki\tentries\tglosses\tlinked\ttranslations\twith_sense\n";
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
            // A wiki without table glosses is linked as it is (no decoding, encoding)
            std::atomic<bool> glosses{false};
            forBlocks(reader, threads, [&](size_t, size_t block) {
                if (glosses)
                    return;
                for (const auto &line : reader.lines(block))
                    if (line.find("\"gloss\":") != std::string::npos) {
                        glosses = true;
                        return;
                    }
            });
            if (!glosses) {
                const std::string base = path.string().substr(0, path.string().size() - 4); // <dir>/<wiki>.jsonl
                for (const char *ext : {".zst", ".idx", ".dict"}) {
                    const fs::path from = base + ext, to = output / (wiki + ".jsonl" + ext);
                    if (!fs::exists(from))
                        continue;
                    fs::remove(to);
                    std::error_code error;
                    fs::create_hard_link(from, to, error);
                    if (error)
                        fs::copy_file(from, to, fs::copy_options::overwrite_existing);
                }
                stats << std::format("{}\t0\t0\t0\t0\t0\n", wiki);
                std::cout << std::format("{}: no glosses, linked as it is ({:.0f} s)", wiki, seconds(wikiStarted)) << std::endl;
                continue;
            }
            JsonlWriter out(output / wiki, Compression::zstd, 9, threads, fs::file_size(path));
            std::vector<Count> counts(threads);
            forBlocks(reader, threads, [&](size_t worker, size_t block) {
                for (const auto &line : reader.lines(block)) {
                    json page = json::parse(line);
                    const std::string title = page["title"].get<std::string>();
                    // (the uniform tree: "languages"; the tree of headings: "sections")
                    const bool uniform = page.contains("languages");
                    for (auto &s : page[uniform ? "languages" : "sections"]) {
                        const std::string language = s.contains("code") && s["code"].is_string()
                                                             ? s["code"].get<std::string>()
                                                             : std::string("None");
                        if (uniform)
                            linkUniform(s, title, counts[worker], language, extras);
                        else
                            linkNode(s, title, counts[worker], language, "", extras);
                    }
                    out.add(worker, page.dump(-1, ' ', false, json::error_handler_t::replace), title);
                }
            });
            out.close();
            Count total;
            for (const auto &c : counts)
                total.add(c);
            stats << std::format("{}\t{}\t{}\t{}\t{}\t{}\n", wiki, total.entries, total.glosses, total.linked,
                                 total.translations, total.withSense);
            std::cout << std::format("{}: {} entries (word, part of speech), glosses {} linked {} ({:.0f}%), translations {} with a "
                                         "sense {} ({:.0f}%) ({:.0f} s)",
                                         wiki, total.entries, total.glosses, total.linked,
                                         100.0 * double(total.linked) / double(std::max<size_t>(1, total.glosses)), total.translations,
                                         total.withSense, 100.0 * double(total.withSense) / double(std::max<size_t>(1, total.translations)),
                                         seconds(wikiStarted))
                          << std::endl;
        }
        std::cout << std::format("done in {:.0f} s", seconds(started)) << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    return 0;
}
