// wordnetsample: a sample of jsonl_senses for the wordnet (HANDOFF-4), closed over translations
// (at random only within the seed languages):
//   1. seed languages (en and the languages of --homonyms): in the wiki of each its homonyms
//      (res/wordnet/homonyms.json, reason "homonym"), then native entries of many translations
//      (at random of the top --high-pool) and of a medium number (--medium-min..--medium-max):
//      --high/--medium in en, --other-high/--other-medium in the other seed languages;
//   2. the pages their translations point to: "home" (the term's page in the wiki of its
//      language, "page_title" when another title), and with --seed-foreign "page" (the seed
//      wiki's page of the term: Polish "pies" in en.wikt, as many as the chosen entries);
//   3. mutual translation: native entries of every wiki with a translation into one of the
//      chosen entries of a seed language (into en dog, into pl zamek…);
//   4. a wiki with fewer than --min pages gets its own native entries of many translations;
//   5. --foreign: non-native entries of the chosen native ones in other wikis (pl.wikt "chien",
//      its French section, beside fr.wikt "chien"), up to as many as the native ones of a wiki.
// Pages of known titles are read through the index (only their blocks); steps 1 and 3 read
// every block. Output: plain <out>/<wiki>.jsonl (wiktbrowse) and <out>/selection.tsv.
//
// wordnetsample [-i jsonl_senses] [-o jsonl_wordnet] [--homonyms res/wordnet/homonyms.json]
//               [--high 300] [--medium 300] [--other-high 100] [--other-medium 100]
//               [--high-pool 3000] [--medium-min 10] [--medium-max 40] [--min 200]
//               [--no-seed-foreign] [--no-foreign] [--seed 1] [-j 16]
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "jsonl_blocks.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using jsonlblocks::BlockReader;

namespace {

using Titles = std::unordered_set<std::string>;

// Language codes whose entries are in another wiki (wiktlink: zh for cmn, no for nb)
std::string wikiOf(const std::string &code) {
    if (code == "cmn")
        return "zh";
    if (code == "nb")
        return "no";
    return code;
}

// The translations of the native sections of a page (under a part of speech: "children")
std::vector<const json *> nativeTranslations(const json &page) {
    std::vector<const json *> found;
    std::function<void(const json &)> walk = [&](const json &node) {
        if (auto t = node.find("translations"); t != node.end())
            for (const auto &item : *t)
                found.push_back(&item);
        for (const char *key : {"sections", "children"})
            if (auto c = node.find(key); c != node.end())
                for (const auto &child : *c)
                    walk(child);
    };
    if (auto sections = page.find("sections"); sections != page.end())
        for (const auto &section : *sections)
            if (section.value("native", false))
                walk(section);
    return found;
}

size_t occurrences(const std::string &text, std::string_view what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size()))
        ++n;
    return n;
}

// f(block) over blocks (all, or the given ones) in threads
void forBlocks(size_t count, const std::vector<size_t> *only, size_t threads, const std::function<void(size_t)> &f) {
    std::atomic<size_t> next{0};
    const size_t total = only ? only->size() : count;
    std::exception_ptr error;
    std::mutex errorMutex;
    std::vector<std::thread> pool;
    for (size_t t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            try {
                for (size_t i; (i = next++) < total;)
                    f(only ? (*only)[i] : i);
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

// The blocks holding any of the titles
std::vector<size_t> blocksOf(const std::vector<std::vector<std::string>> &titles, const Titles &wanted) {
    std::vector<size_t> result;
    for (size_t b = 0; b < titles.size(); ++b)
        if (std::ranges::any_of(titles[b], [&](const std::string &t) { return wanted.contains(t); }))
            result.push_back(b);
    return result;
}

struct Row {
    std::string title, line, reason;
};

template <typename T> std::vector<T> sample(std::vector<T> from, size_t n, std::mt19937 &rng) {
    std::shuffle(from.begin(), from.end(), rng);
    from.resize(std::min(n, from.size()));
    return from;
}

} // namespace

int main(int argc, char *argv[]) {
    try {
        fs::path input = "jsonl_senses", output = "jsonl_wordnet";
        fs::path homonymsFile = WIKTPARSE_RES_DIR "/wordnet/homonyms.json";
        size_t high = 300, medium = 300, otherHigh = 100, otherMedium = 100, highPool = 3000, mediumMin = 10,
               mediumMax = 40, minimum = 200, threads = 16;
        bool seedForeign = true, foreign = true;
        unsigned seed = 1;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto number = [&] { return std::stoul(argv[++i]); };
            if (arg == "-i" && i + 1 < argc)
                input = argv[++i];
            else if (arg == "-o" && i + 1 < argc)
                output = argv[++i];
            else if (arg == "--homonyms" && i + 1 < argc)
                homonymsFile = argv[++i];
            else if (arg == "--high" && i + 1 < argc)
                high = number();
            else if (arg == "--medium" && i + 1 < argc)
                medium = number();
            else if (arg == "--other-high" && i + 1 < argc)
                otherHigh = number();
            else if (arg == "--other-medium" && i + 1 < argc)
                otherMedium = number();
            else if (arg == "--high-pool" && i + 1 < argc)
                highPool = number();
            else if (arg == "--medium-min" && i + 1 < argc)
                mediumMin = number();
            else if (arg == "--medium-max" && i + 1 < argc)
                mediumMax = number();
            else if (arg == "--min" && i + 1 < argc)
                minimum = number();
            else if (arg == "--seed" && i + 1 < argc)
                seed = static_cast<unsigned>(number());
            else if (arg == "-j" && i + 1 < argc)
                threads = number();
            else if (arg == "--no-seed-foreign")
                seedForeign = false;
            else if (arg == "--no-foreign")
                foreign = false;
            else {
                std::cout << "usage: wordnetsample [-i jsonl_senses] [-o jsonl_wordnet] [--homonyms homonyms.json]\n"
                             "       [--high 300] [--medium 300] [--other-high 100] [--other-medium 100]\n"
                             "       [--high-pool 3000] [--medium-min 10] [--medium-max 40] [--min 200]\n"
                             "       [--no-seed-foreign] [--no-foreign] [--seed 1] [-j 16]\n";
                return arg == "-h" || arg == "--help" ? 0 : 1;
            }
        }
        std::mt19937 rng(seed);
        std::vector<std::string> wikis;
        for (const auto &entry : fs::directory_iterator(input))
            if (const auto name = entry.path().filename().string(); name.ends_with(".jsonl.zst"))
                wikis.push_back(name.substr(0, name.size() - 10));
        std::ranges::sort(wikis);
        fs::create_directories(output);
        auto pathOf = [&](const std::string &wiki) { return input / (wiki + ".jsonl.zst"); };
        auto exists = [&](const std::string &wiki) { return std::ranges::binary_search(wikis, wiki); };
        std::mutex mutex;

        // 1. seed languages: en and those of the homonyms; their homonyms, then entries by translations
        std::map<std::string, std::map<std::string, std::string>> chosen; // seed wiki -> title -> reason
        chosen["en"];
        if (std::ifstream in(homonymsFile); in) {
            const json homonyms = json::parse(in);
            for (const auto &[lang, words] : homonyms.value("languages", json::object()).items())
                if (exists(lang))
                    for (const auto &word : words)
                        chosen[lang][word.value("word", std::string())] = "homonym";
        } else
            std::cerr << "wordnetsample: no " << homonymsFile.string() << ", seeds en only" << std::endl;
        std::map<std::string, std::unordered_map<std::string, std::string>> seedLines; // seed wiki -> title -> line
        for (auto &[wiki, picks] : chosen) {
            const BlockReader reader(pathOf(wiki));
            const auto titles = reader.titles();
            std::vector<std::pair<size_t, std::string>> counts;
            forBlocks(reader.size(), nullptr, threads, [&](size_t b) {
                std::vector<std::pair<size_t, std::string>> part;
                const auto lines = reader.lines(b);
                for (size_t k = 0; k < lines.size() && k < titles[b].size(); ++k)
                    if (lines[k].find("\"native\":true") != std::string::npos &&
                        lines[k].find("\"translations\":") != std::string::npos)
                        if (const size_t n = nativeTranslations(json::parse(lines[k])).size())
                            part.emplace_back(n, titles[b][k]);
                std::lock_guard lock(mutex);
                counts.insert(counts.end(), part.begin(), part.end());
            });
            std::ranges::sort(counts, std::greater{});
            std::vector<std::string> top;
            for (size_t i = 0; i < counts.size() && top.size() < highPool; ++i)
                if (!picks.contains(counts[i].second)) // (the homonyms are not drawn again)
                    top.push_back(counts[i].second);
            for (const auto &t : sample(top, wiki == "en" ? high : otherHigh, rng))
                picks[t] = "high";
            std::vector<std::string> middle;
            for (const auto &[n, t] : counts)
                if (n >= mediumMin && n <= mediumMax && !picks.contains(t))
                    middle.push_back(t);
            for (const auto &t : sample(middle, wiki == "en" ? medium : otherMedium, rng))
                picks[t] = "medium";
            // their pages (their blocks only)
            Titles wantedHere;
            for (const auto &[t, _] : picks)
                wantedHere.insert(t);
            auto &found = seedLines[wiki];
            const auto blocks = blocksOf(titles, wantedHere);
            forBlocks(0, &blocks, threads, [&](size_t b) {
                const auto lines = reader.lines(b);
                for (size_t k = 0; k < lines.size() && k < titles[b].size(); ++k)
                    if (wantedHere.contains(titles[b][k])) {
                        std::lock_guard lock(mutex);
                        found.emplace(titles[b][k], lines[k]);
                    }
            });
            std::erase_if(picks, [&](const auto &p) { return !found.contains(p.first); }); // (no such page)
            std::cerr << wiki << ": " << counts.size() << " native entries with translations, " << picks.size()
                      << " chosen" << std::endl;
        }
        std::map<std::string, Titles> chosenTitles; // seed language -> its chosen titles (for mutual ones)
        for (const auto &[wiki, picks] : chosen)
            for (const auto &[t, _] : picks)
                chosenTitles[wiki].insert(t);

        // 2. the pages of their translations
        std::map<std::string, Titles> wanted;
        std::map<std::pair<std::string, std::string>, std::string> reason;
        std::map<std::string, std::map<std::string, std::string>> seedForeignCandidates; // seed wiki -> term -> why
        for (const auto &[seedWiki, picks] : chosen)
            for (const auto &[title, _] : picks) {
                const json page = json::parse(seedLines[seedWiki][title]);
                for (const json *t : nativeTranslations(page)) {
                    const std::string lang = t->value("lang", json()).is_string() ? (*t)["lang"].get<std::string>() : "";
                    const std::string term = t->contains("page_title") ? (*t)["page_title"].get<std::string>()
                                                                       : t->value("term", std::string());
                    if (lang.empty() || term.empty())
                        continue;
                    if (t->value("home", false)) {
                        const auto wiki = wikiOf(lang);
                        wanted[wiki].insert(term);
                        reason.emplace(std::pair{wiki, term}, "translation of " + seedWiki + " " + title);
                    }
                    if (seedForeign && t->value("page", false) && !chosenTitles[seedWiki].contains(term))
                        seedForeignCandidates[seedWiki].emplace(term, lang + " translation of " + seedWiki + " " + title);
                }
            }
        // (non-native pages of a seed wiki: as many as its chosen entries at most, like step 5)
        std::map<std::string, Titles> seedForeignTitles;
        for (auto &[seedWiki, candidates] : seedForeignCandidates) {
            std::vector<std::pair<std::string, std::string>> all(candidates.begin(), candidates.end());
            for (auto &[term, why] : sample(std::move(all), chosen[seedWiki].size(), rng)) {
                wanted[seedWiki].insert(term);
                seedForeignTitles[seedWiki].insert(term);
                reason.emplace(std::pair{seedWiki, term}, why);
            }
        }

        // 3, 4. every wiki: the wanted pages, mutual ones (all blocks), candidates for filling
        std::map<std::string, std::vector<Row>> rows;
        std::map<std::string, Titles> native; // wiki -> its chosen native entries
        for (const auto &wiki : wikis) {
            const BlockReader reader(pathOf(wiki));
            const auto titles = reader.titles();
            const Titles &want = wanted[wiki];
            const Titles noTitles;
            const Titles &ownChosen = chosenTitles.contains(wiki) ? chosenTitles[wiki] : noTitles;
            std::map<std::string, std::string> picked, mutual;
            std::map<std::string, std::string> mutualWhy;
            std::vector<std::tuple<size_t, std::string, std::string>> candidates;
            forBlocks(reader.size(), nullptr, threads, [&](size_t b) {
                std::map<std::string, std::string> p, m, w;
                std::vector<std::tuple<size_t, std::string, std::string>> c;
                const auto lines = reader.lines(b);
                for (size_t k = 0; k < lines.size() && k < titles[b].size(); ++k) {
                    const auto &title = titles[b][k];
                    const auto &line = lines[k];
                    if (ownChosen.contains(title))
                        continue;
                    if (want.contains(title)) {
                        p.emplace(title, line);
                        continue;
                    }
                    if (line.find("\"native\":true") == std::string::npos ||
                        line.find("\"translations\":") == std::string::npos)
                        continue;
                    // a translation into a chosen entry of a seed language (of another wiki)
                    std::string into;
                    for (const json *t : nativeTranslations(json::parse(line))) {
                        const std::string lang = t->value("lang", json()).is_string() ? (*t)["lang"].get<std::string>() : "";
                        if (lang == wiki)
                            continue;
                        if (auto it = chosenTitles.find(wikiOf(lang));
                            it != chosenTitles.end() && it->second.contains(t->value("term", std::string()))) {
                            into = wikiOf(lang) + " " + t->value("term", std::string());
                            break;
                        }
                    }
                    if (!into.empty()) {
                        m.emplace(title, line);
                        w.emplace(title, "translates into " + into);
                        continue;
                    }
                    c.emplace_back(occurrences(line, "\"term\":"), title, line);
                }
                // (of a block its best candidates only)
                std::ranges::sort(c, std::greater{});
                if (c.size() > minimum * 3)
                    c.resize(minimum * 3);
                std::lock_guard lock(mutex);
                picked.merge(p);
                mutual.merge(m);
                mutualWhy.merge(w);
                candidates.insert(candidates.end(), std::make_move_iterator(c.begin()), std::make_move_iterator(c.end()));
            });
            auto &own = rows[wiki];
            if (auto it = chosen.find(wiki); it != chosen.end())
                for (const auto &[t, kind] : it->second)
                    own.push_back({t, seedLines[wiki][t], kind});
            for (auto &[t, line] : picked)
                own.push_back({t, std::move(line), reason[{wiki, t}]});
            for (auto &[t, line] : mutual)
                own.push_back({t, std::move(line), mutualWhy[t]});
            if (own.size() < minimum) {
                std::ranges::sort(candidates, std::greater{});
                if (candidates.size() > minimum * 3)
                    candidates.resize(minimum * 3);
                for (auto &[n, t, line] : sample(std::move(candidates), minimum - own.size(), rng))
                    own.push_back({t, std::move(line), "filling: " + std::to_string(n) + " terms"});
            }
            // (the non-native pages of a seed wiki are not its native entries)
            for (const auto &r : own)
                if (!seedForeignTitles[wiki].contains(r.title))
                    native[wiki].insert(r.title);
            std::cerr << wiki << ": " << picked.size() << " translations, " << mutual.size() << " mutual, " << own.size()
                      << " pages" << std::endl;
        }

        // 5. non-native entries of the chosen native ones in other wikis (their blocks only)
        std::map<std::string, std::vector<Row>> others;
        if (foreign) {
            Titles all;
            for (const auto &[_, titles] : native)
                all.insert(titles.begin(), titles.end());
            for (const auto &wiki : wikis) {
                const BlockReader reader(pathOf(wiki));
                const auto titles = reader.titles();
                const auto blocks = blocksOf(titles, all);
                auto &found = others[wiki];
                forBlocks(0, &blocks, threads, [&](size_t b) {
                    std::vector<Row> part;
                    const auto lines = reader.lines(b);
                    for (size_t k = 0; k < lines.size() && k < titles[b].size(); ++k) {
                        const auto &title = titles[b][k];
                        if (!all.contains(title) || lines[k].find("\"native\":false") == std::string::npos)
                            continue;
                        const json page = json::parse(lines[k]);
                        for (const auto &section : page.value("sections", json::array())) {
                            const std::string code = section.value("code", std::string());
                            if (section.value("native", true) || code.empty() || wikiOf(code) == wiki)
                                continue;
                            if (auto it = native.find(wikiOf(code)); it != native.end() && it->second.contains(title)) {
                                part.push_back({title, lines[k], "non-native " + code + " beside the " + wikiOf(code) + " wiki"});
                                break;
                            }
                        }
                    }
                    std::lock_guard lock(mutex);
                    found.insert(found.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
                });
                std::ranges::sort(found, {}, &Row::title); // (the same sample for the same input)
            }
        }

        std::ofstream selection(output / "selection.tsv");
        selection << "wiki\ttitle\treason\n";
        size_t total = 0;
        for (const auto &wiki : wikis) {
            auto &own = rows[wiki];
            std::set<std::string> taken;
            for (const auto &r : own)
                taken.insert(r.title);
            std::vector<Row> extra;
            for (auto &r : others[wiki])
                if (!taken.contains(r.title))
                    extra.push_back(std::move(r));
            const size_t nativeCount = native[wiki].size(), nonNative = own.size() - nativeCount;
            extra = sample(std::move(extra), nativeCount > nonNative ? nativeCount - nonNative : 0, rng);
            std::ofstream out(output / (wiki + ".jsonl"), std::ios::binary);
            for (const auto *part : {&own, &extra})
                for (const auto &r : *part) {
                    out << r.line << '\n';
                    selection << wiki << '\t' << r.title << '\t' << r.reason << '\n';
                }
            total += own.size() + extra.size();
            std::cerr << wiki << ": " << own.size() << " + " << extra.size() << " non-native" << std::endl;
        }
        std::cout << wikis.size() << " wikis, " << total << " pages -> " << output.string() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "wordnetsample: " << e.what() << std::endl;
        return 1;
    }
}
