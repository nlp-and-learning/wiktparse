// Collects section headers (==header==) of all Wiktionaries and classifies each
// header level: linguistic terms, headers containing the entry word, or free text.
//
// Output directory (default "headers"):
//   report.tsv        one line per wiki, namespace and level with statistics and
//                     verdict; level 0 summarizes lines with one template only
//   <lang>.tsv        namespace, level, pages, cleaned header (on >= 2 pages)
//   vocab.tsv         wiki, namespace, kind, token, pages - from header levels
//                     judged linguistic, summed over levels; kind is text (header
//                     text) or template/arg for headers written as templates
#include <algorithm>
#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <wikilib/dump/dump_path.h>
#include <wikilib/dump/page_handler.h>
#include <wikilib/markup/tokenizer.h>

#include "util/textUtils.h"
#include "wikitext/HeaderTerms.h"

namespace fs = std::filesystem;
using namespace wikilib::dump;

namespace {

// Level 0: a line consisting of one template only, e.g. {{-sost-|it}};
// some Wiktionaries mark sections this way instead of ==headers==
constexpr int templateLine = 0;
constexpr size_t minPages = 2;      // headers written to <lang>.tsv
constexpr size_t minVocabPages = 3; // tokens written to vocab.tsv

struct LevelStats {
    size_t occurrences = 0;
    size_t withTitle = 0;
    std::unordered_map<std::string, size_t> pages; // cleaned header -> pages
};

using NamespaceId = wikilib::NamespaceId;

struct NamespaceStats {
    std::string name;
    size_t pages = 0;
    std::map<int, LevelStats> levels;
};

struct WikiStats {
    std::string lang;
    std::map<NamespaceId, NamespaceStats> namespaces;
};

std::string collapseSpaces(const std::string &text) {
    std::string out;
    bool space = false;
    for (char c : text) {
        if (c == ' ' || c == '\t') {
            space = !out.empty();
            continue;
        }
        if (space)
            out += ' ';
        space = false;
        out += c;
    }
    return out;
}

// Removes the entry word from headers like "kot ({{język polski}})"
std::string withoutTitle(const std::string &header, const std::string &title) {
    std::string text = header;
    if (!title.empty() && text.starts_with(title))
        text = trim(text.substr(title.size()));
    if (text.size() >= 2 && text.front() == '(' && text.back() == ')')
        text = trim(text.substr(1, text.size() - 2));
    return text;
}

// Header text without trailing numbering: "Etymology 1" -> "Etymology"
std::string withoutNumber(std::string text) {
    static const std::regex number(R"(\s+\d+\s*$)");
    return std::regex_replace(text, number, "");
}

// Entry word of a title: without namespace prefix and parent pages
// ("Reconstruction:Proto-Germanic/hundaz" -> "hundaz")
std::string entryWord(const std::string &title, NamespaceId ns) {
    std::string word = title;
    if (ns != 0) {
        if (auto colon = word.find(':'); colon != std::string::npos)
            word = word.substr(colon + 1);
        if (auto slash = word.rfind('/'); slash != std::string::npos && slash + 1 < word.size())
            word = word.substr(slash + 1);
    }
    return word;
}

void scanPage(const std::string &title, const std::string &content, NamespaceStats &stats) {
    stats.pages++;
    std::set<std::pair<int, std::string>> seen;
    for (const auto &rawLine : splitLines(content)) {
        auto line = trim(rawLine);
        int level;
        std::string text;
        if (line.size() >= 3 && line.front() == '=' && line.back() == '=') {
            size_t left = line.find_first_not_of('=');
            if (left == std::string::npos)
                continue;
            size_t right = line.size() - 1 - line.find_last_not_of('=');
            level = static_cast<int>(std::min(left, right));
            text = trim(line.substr(level, line.size() - 2 * level));
        } else if (line.starts_with("{{") && line.ends_with("}}") && line.find("}}") == line.size() - 2) {
            level = templateLine;
            text = line;
        } else
            continue;
        if (text.empty())
            continue;
        auto &levelStats = stats.levels[level];
        levelStats.occurrences++;
        bool hasTitle = !title.empty() && text.find(title) != std::string::npos;
        if (hasTitle)
            levelStats.withTitle++;
        auto cleaned = collapseSpaces(withoutNumber(hasTitle ? withoutTitle(text, title) : text));
        if (seen.insert({level, cleaned}).second)
            levelStats.pages[cleaned]++;
    }
}

WikiStats scanWiki(const std::string &lang, const fs::path &dumpFile) {
    WikiStats stats;
    stats.lang = lang;
    PageHandler handler(dumpFile.string());
    PageFilter filter;
    filter.include_redirects = false;
    while (auto page = handler.next_page(filter)) {
        auto ns = page->info.namespace_id;
        // Talk and user pages are free text; skip non-wikitext (Lua modules, CSS)
        if (ns < 0 || ns % 2 == 1 || ns == 2)
            continue;
        if (auto *rev = page->latest_revision(); !rev || !rev->is_wikitext())
            continue;
        auto &nsStats = stats.namespaces[ns];
        if (nsStats.name.empty()) {
            nsStats.name = ns == 0 ? "(main)" : std::to_string(ns);
            for (const auto &n : handler.site_info().namespaces)
                if (n.id == ns && !n.name.empty())
                    nsStats.name = n.name;
        }
        auto content = wikilib::markup::strip_comments(page->content());
        scanPage(entryWord(page->info.title, ns), content, nsStats);
    }
    if (!handler.error().empty())
        throw std::runtime_error(std::string(handler.error()));
    return stats;
}

// At most maxBytes, not cutting a UTF-8 character
std::string utf8Prefix(const std::string &text, size_t maxBytes) {
    if (text.size() <= maxBytes)
        return text;
    size_t end = maxBytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
        end--;
    return text.substr(0, end);
}

// Share of header occurrences whose (cleaned) header repeats on minVocabPages pages
double repeatedShare(const LevelStats &s) {
    size_t repeated = 0, all = 0;
    for (const auto &[header, pages] : s.pages) {
        all += pages;
        if (pages >= minVocabPages)
            repeated += pages;
    }
    return all ? double(repeated) / all : 0;
}

// Trusted levels have few distinct, often repeated headers
std::string verdict(int level, const LevelStats &s) {
    if (s.occurrences < 20)
        return "rare";
    if (repeatedShare(s) < 0.8)
        return "free";
    if (level == templateLine)
        return "template-lines";
    if (double(s.withTitle) / s.occurrences > 0.5)
        return "word+term"; // header has the entry word; cleaned header used
    return "terms";
}

bool linguisticVerdict(const std::string &v) {
    return v == "terms" || v == "word+term";
}

void writeResults(const fs::path &outDir, const std::vector<WikiStats> &all) {
    std::ofstream report(outDir / "report.tsv");
    report << "wiki\tnamespace\tpages\tlevel\toccurrences\tdistinct\tdistinct%\ttitle%\trepeated%\theaders/page\t"
              "verdict\ttop\n";
    std::ofstream vocab(outDir / "vocab.tsv");
    vocab << "wiki\tnamespace\tkind\ttoken\tpages\n";
    for (const auto &wiki : all) {
        std::ofstream headers(outDir / (wiki.lang + ".tsv"));
        headers << "namespace\tlevel\tpages\theader\n";
        for (const auto &[ns, stats] : wiki.namespaces) {
        // Tokens of all levels of the namespace: (kind, token) -> pages
        std::map<std::pair<std::string, std::string>, size_t> tokens;
        for (const auto &[level, s] : stats.levels) {
            std::vector<std::pair<std::string, size_t>> sorted(s.pages.begin(), s.pages.end());
            std::ranges::sort(sorted, [](const auto &a, const auto &b) {
                return a.second != b.second ? a.second > b.second : a.first < b.first;
            });
            auto v = verdict(level, s);
            std::string top;
            for (size_t i = 0; i < std::min<size_t>(6, sorted.size()); i++)
                top += (i ? "; " : "") + utf8Prefix(sorted[i].first, 40) + " x" + std::to_string(sorted[i].second);
            report << std::format("{}\t{}\t{}\t{}\t{}\t{}\t{:.1f}\t{:.1f}\t{:.1f}\t{:.2f}\t{}\t{}\n", wiki.lang,
                                  stats.name, stats.pages, level, s.occurrences, s.pages.size(),
                                  s.occurrences ? 100.0 * s.pages.size() / s.occurrences : 0.0,
                                  s.occurrences ? 100.0 * s.withTitle / s.occurrences : 0.0,
                                  100.0 * repeatedShare(s),
                                  stats.pages ? double(s.occurrences) / stats.pages : 0.0, v, top);
            // Template lines are not headers; only summarized in the report
            if (level == templateLine)
                continue;
            // On "terms" levels even a header of one page is a term (rare language)
            const size_t minHeaderPages = v == "terms" ? 1 : minPages;
            const size_t minTokenPages = v == "terms" ? 1 : minVocabPages;
            for (const auto &[header, pages] : sorted) {
                if (pages >= minHeaderPages)
                    headers << stats.name << '\t' << level << '\t' << pages << '\t' << header << '\n';
                if (!linguisticVerdict(v) || pages < minTokenPages)
                    continue;
                // Header text; for headers written as templates also template names and arguments
                auto t = tokenizeHeader(header);
                for (const auto &x : t.texts) tokens[{"text", x}] += pages;
                for (const auto &tpl : t.templates) {
                    tokens[{"template", tpl.name}] += pages;
                    for (const auto &arg : tpl.args)
                        if (!arg.empty())
                            tokens[{"arg", arg}] += pages;
                }
            }
        }
        for (const auto &[key, pages] : tokens)
            vocab << wiki.lang << '\t' << stats.name << '\t' << key.first << '\t' << key.second << '\t' << pages
                  << '\n';
        }
    }
}

} // namespace

int main(int argc, char *argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    size_t threads = 8;
    if (args.size() >= 2 && args[0] == "-j") {
        threads = std::max<size_t>(1, std::stoul(args[1]));
        args.erase(args.begin(), args.begin() + 2);
    }
    if (!args.empty() && (args[0] == "-h" || args[0] == "--help")) {
        std::cout << "usage: wiktheaders [-j N] [output_dir] [lang...]   (default: headers, all Wiktionaries)\n";
        return 0;
    }
    try {
        fs::path outDir = args.empty() ? "headers" : args[0];
        auto path = DumpPath::from_config();
        path.set_project(WikiProject::Wiktionary);
        std::vector<std::string> langs(args.size() > 1 ? args.begin() + 1 : args.end(), args.end());
        if (langs.empty())
            langs = path.available_languages();
        // Largest dumps first, so they do not finish last
        std::vector<std::pair<uint64_t, std::string>> jobs;
        for (const auto &lang : langs) {
            auto p = path;
            p.set_language(lang);
            if (p.dump_exists())
                jobs.emplace_back(p.dump_size(), lang);
            else
                std::cerr << "no dump for " << lang << '\n';
        }
        std::ranges::sort(jobs, std::greater<>());
        fs::create_directories(outDir);
        std::cout << jobs.size() << " Wiktionaries, " << threads << " threads, output " << outDir << std::endl;

        std::vector<WikiStats> all(jobs.size());
        std::atomic<size_t> next{0}, done{0};
        std::mutex outMutex;
        auto work = [&] {
            for (size_t i; (i = next++) < jobs.size();) {
                auto p = path;
                p.set_language(jobs[i].second);
                try {
                    all[i] = scanWiki(jobs[i].second, p.dump_path());
                } catch (const std::exception &e) {
                    all[i].lang = jobs[i].second;
                    std::lock_guard lock(outMutex);
                    std::cerr << jobs[i].second << ": " << e.what() << '\n';
                }
                std::lock_guard lock(outMutex);
                size_t pages = 0;
                for (const auto &[ns, s] : all[i].namespaces)
                    pages += s.pages;
                std::cout << ++done << "/" << jobs.size() << " " << jobs[i].second << " (" << pages << " pages)"
                          << std::endl;
            }
        };
        std::vector<std::thread> pool;
        for (size_t t = 0; t < std::min(threads, jobs.size()); t++)
            pool.emplace_back(work);
        for (auto &t : pool)
            t.join();
        std::ranges::sort(all, {}, &WikiStats::lang);
        writeResults(outDir, all);
        std::cout << "written " << (outDir / "report.tsv").string() << ", vocab.tsv, <lang>.tsv" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
