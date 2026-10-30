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
            {"ngd", 1},   {"n-g", 1},    {"non-gloss definition", 1}, {"non-gloss", 1}, {"sense", 1},     {"s", 1}};
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
        } else if (!t.name.ends_with(" of") && t.name != "lb" && t.name != "place" && [&] {
                       // Another template of a sentence: its text ({{non-gloss|The third-person …}})
                       int sentences = 0;
                       for (const auto &[key, value] : t.parameters)
                           sentences += key.empty() || std::isdigit(static_cast<unsigned char>(key[0]))
                                                ? value.find(' ') != std::string::npos && value.size() > 15
                                                : 0;
                       return sentences == 1;
                   }()) {
            for (const auto &[key, value] : t.parameters)
                if ((key.empty() || std::isdigit(static_cast<unsigned char>(key[0]))) && value.find(' ') != std::string::npos &&
                    value.size() > 15)
                    out += " " + value + " ";
        } else if (t.name == "place") {
            // en {{place|en|capital city|c/Guyana|;|named for …}}: "capital city Guyana" (the
            // definition of most proper nouns of places)
            for (size_t p = 2; auto v = t.get(p); ++p) {
                std::string part(*v);
                if (part == ";" || part.starts_with("named"))
                    break;
                if (auto slash = part.find('/'); slash != std::string::npos && slash < 6)
                    part = part.substr(slash + 1);
                out += " " + part + " ";
            }
        } else if (t.name.ends_with(" of") || t.name == "surname" || t.name == "given name" ||
                   t.name == "historical given name" || t.name == "patronymic") {
            // Definitions of form-of templates as their words: {{initialism of|en|cumulative
            // distribution function}}, {{given name|en|male}} -> "given name male", {{surname|en}}
            out += " " + t.name + " ";
            for (size_t p = 2; auto v = t.get(p); ++p)
                if (const std::string part(*v); !part.empty() && part.size() < 80)
                    out += part + " ";
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

// Links found by embeddings and LLMs (glossembed.py --apply, glossllm.py; the accepted ones) -> sense
// id, score. Of the tree of headings (wikt2jsonl): title, language, path of the node
// ("sections:2/children:0"), gloss; of the uniform tree (wikt2jsonl --uniform; the TSV of
// glossrekey.py, a column "pos" for "path"): title, language, part of speech, gloss, of the entry whose
// senses have its ids (two entries of a part of speech: en Etymology 1 and 2)
struct Extra {
    std::string id;
    double score;
    std::string method; // embedding (glossembed-*), llm (glossllm-*), minority, vote, manual
};
struct Extras {
    std::map<std::string, Extra> byPath;
    std::multimap<std::string, Extra> byPos;
    [[nodiscard]] size_t size() const { return byPath.size() + byPos.size(); }
};

std::string extraKey(const std::string &title, const std::string &language, const std::string &where,
                     const std::string &gloss) {
    return title + '\t' + language + '\t' + where + '\t' + gloss;
}

Extras readExtras(const std::vector<fs::path> &files) {
    Extras extras;
    auto fields = [](const std::string &line) {
        std::vector<std::string> f;
        for (size_t from = 0;;) {
            const size_t tab = line.find('\t', from);
            f.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
            if (tab == std::string::npos)
                break;
            from = tab + 1;
        }
        return f;
    };
    for (const auto &file : files) {
        std::ifstream in(file);
        std::string line;
        std::getline(in, line); // header: wiki title language path|pos gloss sense_id score margin accepted
        const auto header = fields(line);
        const bool byPos = header.size() > 3 && header[3] == "pos";
        const std::string name = file.filename().string();
        const std::string method = name.ends_with(".minority.tsv")       ? "minority"
                                   : name.ends_with(".voted.chosen.tsv") ? "vote"
                                   : name.ends_with(".chosen.tsv")       ? "manual"
                                   : name.starts_with("glossllm")        ? "llm"
                                                                         : "embedding";
        while (std::getline(in, line))
            if (const auto f = fields(line); f.size() >= 9 && f[8] == "1") {
                const Extra extra{f[5], std::stod(f[6]), method};
                if (byPos)
                    extras.byPos.emplace(extraKey(f[1], f[2], f[3], f[4]), extra);
                else
                    extras.byPath[extraKey(f[1], f[2], f[3], f[4])] = extra;
            }
    }
    return extras;
}

// The ids of a link ("2,3": a gloss of several definitions, en tourmaline "both senses")
std::vector<std::string> extraIds(const Extra &extra) {
    std::vector<std::string> ids;
    for (size_t from = 0; from <= extra.id.size();) {
        const size_t comma = std::min(extra.id.find(',', from), extra.id.size());
        if (comma > from)
            ids.push_back(extra.id.substr(from, comma - from));
        from = comma + 1;
    }
    return ids;
}

// The glosses of the translations of an entry (translations: the arrays of its tables) linked to its
// senses (defs); extra: the link of embeddings or an LLM of a gloss left, if any
void linkEntry(defglos::Entry &entry, const std::vector<json *> &translations, Count &count,
               const std::function<const Extra *(const std::string &gloss)> &extra) {
    // The distinct glosses in order, of translations without a sense number of the wiki
    std::map<std::string, size_t> glossIndex;
    for (json *list : translations)
        for (const auto &t : *list)
            if (t.contains("gloss") && !t.contains("sense"))
                if (const std::string g = t["gloss"].get<std::string>(); !glossIndex.contains(g)) {
                    glossIndex[g] = entry.glosses.size();
                    entry.glosses.push_back({std::to_string(entry.glosses.size() + 1), plainText(g)});
                }
    if (entry.glosses.empty() || entry.defs.empty())
        return;
    ++count.entries;
    count.glosses += entry.glosses.size();
    // Per gloss: the definitions, how, how sure
    struct Result {
        std::vector<std::string> ids;
        std::string method;
        double score = 0;
    };
    std::vector<Result> results(entry.glosses.size());
    // A gloss that is a sense number: ja "語義1", "1.", "sense 2"
    // (ja "語義1:", "名詞語義1", "意味2", "語義1・2"; multibyte marks as alternatives, not in brackets)
    static const std::regex number(
            R"(^\s*(?:名詞|動詞|形容詞)?\s*(?:語義|意味|sense|meaning|znaczenie)?\s*(\d+(?:\.\d+)*)(?:\s*(?:・|,|、)\s*(\d+(?:\.\d+)*))?\s*(?:\.|:|：)?\s*$)",
            std::regex::icase);
    for (size_t g = 0; g < entry.glosses.size(); ++g)
        if (std::smatch m; std::regex_match(entry.glosses[g].text, m, number)) {
            std::vector<std::string> ids;
            for (size_t k : {1u, 2u})
                if (m[k].matched && std::ranges::any_of(entry.defs, [&](const auto &d) { return d.id == m[k].str(); }))
                    ids.push_back(m[k].str());
            if (!ids.empty())
                results[g] = {ids, "number", 1.0};
        }
    // The definitions and glosses by their words (defglos)
    const defglos::Similarity similarity(entry);
    for (const auto &link : defglos::match(entry, similarity))
        if (auto &r = results[link.gloss]; r.method.empty() || r.method != "number") {
            if (r.method.empty())
                r.method = std::string(defglos::methodName(link.method));
            r.ids.push_back(entry.defs[link.def].id);
            r.score = std::max(r.score, link.score);
        }
    // By embeddings and LLMs, for the glosses left (glossembed.py, glossllm.py)
    for (const auto &[gloss, g] : glossIndex)
        if (results[g].ids.empty())
            if (const Extra *e = extra(gloss))
                results[g] = {extraIds(*e), e->method, e->score};
    // One definition only: every gloss is of it (en Panama: "country")
    if (entry.defs.size() == 1)
        for (auto &r : results)
            if (r.ids.empty())
                r = {{entry.defs[0].id}, "single", 1.0};
    for (const auto &r : results)
        count.linked += !r.ids.empty();
    for (json *list : translations)
        for (auto &t : *list) {
            if (!t.contains("gloss") || t.contains("sense"))
                continue;
            ++count.translations;
            const auto &r = results[glossIndex[t["gloss"].get<std::string>()]];
            if (r.ids.empty())
                continue;
            ++count.withSense;
            t["sense_ids"] = r.ids;
            t["sense_method"] = r.method;
            t["sense_score"] = std::round(r.score * 1000) / 1000;
        }
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
        if (c.value("section", "") == "Translations" && c.contains("translations"))
            tables.push_back(&c["translations"]);
    if (tables.empty())
        return;
    defglos::Entry entry;
    entry.title = title;
    entry.pos = node.value("section", "");
    collectSenses(node, entry.defs);
    linkEntry(entry, tables, count, [&](const std::string &gloss) -> const Extra * {
        auto it = extras.byPath.find(extraKey(title, language, path, gloss));
        return it == extras.byPath.end() ? nullptr : &it->second;
    });
}

// The entries of a language of the uniform tree (wikt2jsonl --uniform): the glosses of each one's
// translations linked to its senses
void linkUniform(json &language, const std::string &title, Count &count, const std::string &code,
                 const Extras &extras) {
    if (!language.contains("entries"))
        return;
    for (auto &e : language["entries"]) {
        if (!e.contains("senses") || !e.contains("translations"))
            continue;
        defglos::Entry entry;
        entry.title = title;
        entry.pos = e.value("pos", "");
        for (const auto &s : e["senses"])
            entry.defs.push_back({s.value("id", ""), plainText(s.value("text", ""))});
        // (a link of the part of speech and gloss whose ids are all senses of this entry; of a later
        // file first, as the later one wins of the same path)
        linkEntry(entry, {&e["translations"]}, count, [&](const std::string &gloss) -> const Extra * {
            const auto [from, to] = extras.byPos.equal_range(extraKey(title, code, entry.pos, gloss));
            for (auto it = std::make_reverse_iterator(to); it != std::make_reverse_iterator(from); ++it)
                if (std::ranges::all_of(extraIds(it->second), [&](const std::string &id) {
                        return std::ranges::any_of(entry.defs, [&](const auto &d) { return d.id == id; });
                    }))
                    return &it->second;
            return nullptr;
        });
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
    std::vector<fs::path> extraFiles;
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
        else if (arg == "-h" || arg == "--help") {
            std::cout << "usage: wiktglos [-i jsonl_link] [-o jsonl_senses] [-j 16] [--extra glossembed-en.tsv]... [wiki...]\n"
                         "       input: the tree of headings or the uniform tree (wikt2jsonl --uniform)\n"
                         "       --extra: links by embeddings or LLMs (glossembed.py --apply, glossllm.py), the accepted\n"
                         "       ones; for the uniform tree their TSV by part of speech (glossrekey.py)\n";
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
