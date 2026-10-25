// wiktglos: phase 2 on the output of wiktlink (or wikt2jsonl): the glosses of translation
// tables linked to the definitions (HANDOFF-3). For each part of speech whose translations
// have a table gloss and no sense number (en {{trans-top|domestic species}}; not pl, de, fr
// with numbers), the definitions (senses, wikitext reduced to words) and the distinct glosses
// go to defglos::match (positional alignment, then the best of the rest); a translation of a
// linked gloss gets "sense_ids" (["1", "1.2"]), "sense_method" and "sense_score".
//
// wiktglos [-i jsonl_link] [-o jsonl_senses] [-j 16] [wiki...]
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
#include <wikilib/templates/template_parser.h>

#include "defglos/Matcher.h"
#include "jsonl_blocks.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using jsonlblocks::BlockReader;
using jsonlblocks::Compression;
using jsonlblocks::JsonlWriter;

namespace {

// Wikitext of a definition as words: links as their text, templates of labels left out
// ({{lb|en|transitive}}), of words as their text ({{taxfmt|Canidae|family}} -> Canidae,
// {{l|en|dog}} -> dog), no '' and '''
std::string plainText(const std::string &text) {
    static const std::map<std::string, int> wordTemplates = {
            {"l", 2},     {"m", 2},      {"ll", 2},    {"w", 1},     {"taxfmt", 1}, {"taxlink", 1},
            {"vern", 1},  {"gloss", 1},  {"gl", 1},    {"q", 1},     {"qualifier", 1}, {"i", 1},
            {"ngd", 1},   {"n-g", 1},    {"non-gloss definition", 1}, {"sense", 1},     {"s", 1}};
    std::string out;
    size_t i = 0;
    for (const auto &t : wikilib::templates::find_invocations(text)) {
        const size_t begin = t.location.begin.offset, end = t.location.end.offset;
        if (begin < i)
            continue; // nested
        out += text.substr(i, begin - i);
        if (auto it = wordTemplates.find(t.name); it != wordTemplates.end()) {
            if (auto v = t.get(static_cast<size_t>(it->second)))
                out += " " + std::string(*v) + " ";
        } else if (t.name == "lb" || t.name == "lbl" || t.name == "label" || t.name == "tlb") {
            // Labels as words: glosses begin with them ("snooker: one of the 15 red balls")
            for (size_t p = 2; auto v = t.get(p); ++p)
                if (const std::string label(*v); label != "_" && label != "and" && label != "or" && label != "&")
                    out += " " + label + " ";
            out += ": ";
        }
        i = end;
    }
    out += text.substr(std::min(i, text.size()));
    // Links: [[target|shown]] -> shown, [[target]] -> target
    std::string plain;
    for (size_t k = 0; k < out.size();) {
        if (out.compare(k, 2, "[[") == 0) {
            const size_t close = out.find("]]", k + 2);
            if (close == std::string::npos)
                break;
            std::string link = out.substr(k + 2, close - k - 2);
            if (auto bar = link.rfind('|'); bar != std::string::npos)
                link = link.substr(bar + 1);
            plain += link;
            k = close + 2;
        } else if (out.compare(k, 2, "''") == 0)
            k += 2;
        else
            plain += out[k++];
    }
    return plain;
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

struct Count {
    size_t entries = 0, glosses = 0, linked = 0, translations = 0, withSense = 0;
    void add(const Count &o) {
        entries += o.entries, glosses += o.glosses, linked += o.linked;
        translations += o.translations, withSense += o.withSense;
    }
};

// The senses of a part of speech: its own, and those of its Definitions children (pl-like)
void collectSenses(const json &node, std::vector<defglos::Item> &defs) {
    if (auto it = node.find("senses"); it != node.end())
        for (const auto &s : *it)
            defs.push_back({s.value("id", ""), plainText(s.value("text", ""))});
    // (and of a part of speech of the same name below it: en {{en-verb}} under ===Verb=== of
    // wikt2jsonl before 2026-10-25)
    if (auto it = node.find("children"); it != node.end())
        for (const auto &c : *it)
            if (c.value("section", "") == "Definitions" ||
                (c.value("section", "") == node.value("section", "") && c.contains("senses")))
                collectSenses(c, defs);
}

// A part of speech with senses and Translations children: its glosses linked to its senses
void linkNode(json &node, const std::string &title, Count &count) {
    if (auto it = node.find("children"); it != node.end())
        for (auto &c : *it)
            linkNode(c, title, count);
    if (auto it = node.find("sections"); it != node.end())
        for (auto &c : *it)
            linkNode(c, title, count);
    if (!node.contains("senses") || !node.contains("children"))
        return;
    std::vector<json *> tables;
    for (auto &c : node["children"])
        if (c.value("section", "") == "Translations" && c.contains("translations"))
            tables.push_back(&c);
    if (tables.empty())
        return;
    defglos::Entry entry;
    entry.title = title;
    entry.pos = node.value("section", "");
    collectSenses(node, entry.defs);
    // The distinct glosses in order, of translations without a sense number of the wiki
    std::map<std::string, size_t> glossIndex;
    for (json *table : tables)
        for (const auto &t : (*table)["translations"])
            if (t.contains("gloss") && !t.contains("sense"))
                if (const std::string g = t["gloss"].get<std::string>(); !glossIndex.contains(g)) {
                    glossIndex[g] = entry.glosses.size();
                    entry.glosses.push_back({std::to_string(entry.glosses.size() + 1), plainText(g)});
                }
    if (entry.glosses.empty() || entry.defs.empty())
        return;
    ++count.entries;
    count.glosses += entry.glosses.size();
    const defglos::Similarity similarity(entry);
    std::vector<std::vector<const defglos::Link *>> byGloss(entry.glosses.size());
    const auto links = defglos::match(entry, similarity);
    for (const auto &link : links)
        byGloss[link.gloss].push_back(&link);
    for (const auto &g : byGloss)
        count.linked += !g.empty();
    for (json *table : tables)
        for (auto &t : (*table)["translations"]) {
            if (!t.contains("gloss") || t.contains("sense"))
                continue;
            ++count.translations;
            const auto &found = byGloss[glossIndex[t["gloss"].get<std::string>()]];
            if (found.empty())
                continue;
            ++count.withSense;
            json ids = json::array();
            double score = 0;
            for (const auto *link : found) {
                ids.push_back(entry.defs[link->def].id);
                score = std::max(score, link->score);
            }
            t["sense_ids"] = ids;
            t["sense_method"] = std::string(defglos::methodName(found.front()->method));
            t["sense_score"] = std::round(score * 1000) / 1000;
        }
}

double seconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

} // namespace

int main(int argc, char *argv[]) {
    fs::path input = "jsonl_link", output = "jsonl_senses";
    size_t threads = 16;
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-i" && i + 1 < argc)
            input = argv[++i];
        else if (arg == "-o" && i + 1 < argc)
            output = argv[++i];
        else if (arg == "-j" && i + 1 < argc)
            threads = std::max(1, std::stoi(argv[++i]));
        else if (arg == "-h" || arg == "--help") {
            std::cout << "usage: wiktglos [-i jsonl_link] [-o jsonl_senses] [-j 16] [wiki...]\n";
            return 0;
        } else
            only.push_back(arg);
    }
    try {
        const auto started = std::chrono::steady_clock::now();
        std::map<std::string, fs::path> files;
        for (const auto &entry : fs::directory_iterator(input)) {
            const std::string name = entry.path().filename().string();
            if (name.ends_with(".jsonl.zst"))
                files[name.substr(0, name.size() - 10)] = entry.path();
        }
        if (files.empty())
            throw std::runtime_error("no <wiki>.jsonl.zst in " + input.string());
        fs::create_directories(output);
        std::ofstream stats(output / "stats.tsv");
        stats << "wiki\tentries\tglosses\tlinked\ttranslations\twith_sense\n";
        for (const auto &[wiki, path] : files) {
            if (!only.empty() && !std::ranges::contains(only, wiki))
                continue;
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
                    for (auto &s : page["sections"])
                        linkNode(s, title, counts[worker]);
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
