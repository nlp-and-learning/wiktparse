// Builds term lists of each Wiktionary from wiktheaders output, without reading
// dumps: language names, parts of speech and section names ("Etymology",
// "Synonyms", "verbe") to find Wikidata entities by their labels.
//
// Input: <headers>/report.tsv and <headers>/<wiki>.tsv
// Output: <terms>/<wiki>.txt - one term per line, most frequent first
//
// Only trusted header levels are used: all headers of "terms" levels, headers
// on >= 3 pages of "word+term" levels; by default main namespace only. Headers written as
// templates give the template name and positional arguments, except
// argument positions holding language codes ({{S|verbe|sco}} -> verbe).
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "wikitext/HeaderTerms.h"

namespace fs = std::filesystem;

namespace {

// Headers on fewer pages are rare: on "word+term" levels never used (failed
// removals of the word); on "terms" levels used (e.g. languages of one page)
// unless there are many more of them than repeated ones (words in headers)
constexpr size_t minRepeatedPages = 3;
constexpr double maxRareToRepeated = 3.0;
constexpr size_t minCodePositionValues = 5;
constexpr double minCodeShare = 0.8;

std::vector<std::string> splitTabs(const std::string &line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    for (std::string field; std::getline(ss, field, '\t');)
        fields.push_back(field);
    return fields;
}

using LevelKey = std::tuple<std::string, std::string, int>; // wiki, namespace, level

// Verdict of trusted header levels: "terms" or "word+term"
std::map<LevelKey, std::string> trustedLevels(const fs::path &report) {
    std::ifstream in(report);
    if (!in) throw std::runtime_error("Cannot open " + report.string());
    std::map<LevelKey, std::string> trusted;
    std::string line;
    std::getline(in, line); // header
    while (std::getline(in, line)) {
        auto f = splitTabs(line);
        if (f.size() > 10 && (f[10] == "terms" || f[10] == "word+term"))
            trusted[{f[0], f[1], std::stoi(f[3])}] = f[10];
    }
    return trusted;
}

bool usableTerm(const std::string &term);

// Template name as a term without -/= marks ("=inggris=" -> "inggris"); empty
// when not usable: "-el-" or "=kab=" are language codes, "S" too short
std::string templateNameTerm(const std::string &name) {
    auto b = name.find_first_not_of("-=");
    if (b == std::string::npos)
        return "";
    auto core = name.substr(b, name.find_last_not_of("-=") - b + 1);
    if (core.size() < 2 || looksLikeLanguageCode(core) || !usableTerm(core))
        return "";
    return core;
}

bool usableTerm(const std::string &term) {
    if (term.empty() || std::ranges::all_of(term, [](unsigned char c) { return std::isdigit(c) || c == ' '; }))
        return false;
    // Remains of broken markup: "{S|paro}}", "[[x"
    if (term.find_first_of("{}[]|<>") != std::string::npos)
        return false;
    // Short Latin fragments ("ad", "me") match random labels; CJK terms may be short
    bool ascii = std::ranges::all_of(term, [](unsigned char c) { return c < 0x80; });
    return !(ascii && term.size() < 3);
}

struct WikiTermList {
    std::vector<std::pair<std::string, size_t>> terms; // most frequent first
    std::set<std::string> codes;                       // values at language code positions
};

WikiTermList wikiTerms(const std::string &wiki, const fs::path &file,
                       const std::map<LevelKey, std::string> &trusted,
                       const std::set<std::string> &namespaces) {
    WikiTermList result;
    struct Row {
        std::string ns;
        int level;
        size_t pages;
        std::string header;
    };
    std::vector<Row> rows;
    std::map<std::pair<std::string, int>, std::pair<size_t, size_t>> counts; // (ns, level) -> repeated, rare
    std::ifstream in(file);
    std::string line;
    std::getline(in, line); // header
    while (std::getline(in, line)) {
        auto f = splitTabs(line);
        if (f.size() < 4 || !namespaces.contains(f[0]))
            continue;
        Row row{f[0], std::stoi(f[1]), std::stoul(f[2]), f[3]};
        auto &count = counts[{row.ns, row.level}];
        (row.pages >= minRepeatedPages ? count.first : count.second)++;
        rows.push_back(std::move(row));
    }
    std::vector<std::pair<HeaderTokens, size_t>> headers;
    for (const auto &row : rows) {
        auto verdict = trusted.find({wiki, row.ns, row.level});
        if (verdict == trusted.end())
            continue;
        if (row.pages < minRepeatedPages) {
            auto [repeated, rare] = counts[{row.ns, row.level}];
            if (verdict->second != "terms" || rare > maxRareToRepeated * repeated)
                continue;
        }
        headers.emplace_back(tokenizeHeader(row.header), row.pages);
    }

    // Argument positions with many values that are mostly language codes
    std::map<std::pair<std::string, size_t>, std::set<std::string>> positionValues;
    for (const auto &[tokens, pages] : headers)
        for (const auto &tpl : tokens.templates)
            for (size_t i = 0; i < tpl.args.size(); i++)
                if (!tpl.args[i].empty())
                    positionValues[{tpl.name, i}].insert(tpl.args[i]);
    std::set<std::pair<std::string, size_t>> codePositions;
    for (const auto &[position, values] : positionValues) {
        auto codes = std::ranges::count_if(values, looksLikeLanguageCode);
        if (values.size() >= minCodePositionValues && codes >= minCodeShare * values.size()) {
            codePositions.insert(position);
            std::ranges::copy_if(values, std::inserter(result.codes, result.codes.end()), looksLikeLanguageCode);
        }
    }

    std::unordered_map<std::string, size_t> terms;
    for (const auto &[tokens, pages] : headers) {
        for (const auto &text : tokens.texts)
            if (usableTerm(text))
                terms[text] += pages;
        for (const auto &tpl : tokens.templates) {
            if (auto name = templateNameTerm(tpl.name); !name.empty())
                terms[name] += pages;
            for (size_t i = 0; i < tpl.args.size(); i++)
                if (usableTerm(tpl.args[i]) && !codePositions.contains({tpl.name, i}))
                    terms[tpl.args[i]] += pages;
        }
    }
    result.terms.assign(terms.begin(), terms.end());
    std::ranges::sort(result.terms, [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    return result;
}

} // namespace

int main(int argc, char *argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    // Extra namespaces: --ns fr:Reconstruction
    std::map<std::string, std::set<std::string>> extraNamespaces;
    for (size_t i = 0; i + 1 < args.size();) {
        if (args[i] == "--ns") {
            auto spec = args[i + 1];
            auto colon = spec.find(':');
            if (colon == std::string::npos) {
                std::cerr << "--ns needs wiki:Namespace\n";
                return 1;
            }
            extraNamespaces[spec.substr(0, colon)].insert(spec.substr(colon + 1));
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else
            i++;
    }
    if (!args.empty() && (args[0] == "-h" || args[0] == "--help")) {
        std::cout << "usage: wiktterms [headers_dir] [terms_dir] [--ns wiki:Namespace]...\n"
                     "       defaults: headers terms; main namespace only\n";
        return 0;
    }
    try {
        fs::path headersDir = args.size() > 0 ? args[0] : "headers";
        fs::path termsDir = args.size() > 1 ? args[1] : "terms";
        auto trusted = trustedLevels(headersDir / "report.tsv");
        fs::create_directories(termsDir);
        size_t wikis = 0, total = 0;
        std::ofstream summary(termsDir / "summary.tsv");
        summary << "wiki\tterms\n";
        std::vector<fs::path> files;
        for (const auto &entry : fs::directory_iterator(headersDir))
            if (entry.path().extension() == ".tsv" && entry.path().stem() != "report" &&
                entry.path().stem() != "vocab")
                files.push_back(entry.path());
        std::ranges::sort(files);
        std::vector<std::pair<std::string, WikiTermList>> all;
        std::set<std::string> codes; // language codes seen in any Wiktionary
        for (const auto &file : files) {
            auto wiki = file.stem().string();
            std::set<std::string> namespaces = {"(main)"};
            if (auto extra = extraNamespaces.find(wiki); extra != extraNamespaces.end())
                namespaces.insert(extra->second.begin(), extra->second.end());
            auto terms = wikiTerms(wiki, file, trusted, namespaces);
            codes.insert(terms.codes.begin(), terms.codes.end());
            all.emplace_back(wiki, std::move(terms));
        }
        for (const auto &[wiki, terms] : all) {
            std::vector<std::string> kept;
            for (const auto &[term, pages] : terms.terms)
                if (!codes.contains(term))
                    kept.push_back(term);
            if (kept.empty())
                continue;
            std::ofstream out(termsDir / (wiki + ".txt"));
            for (const auto &term : kept)
                out << term << '\n';
            summary << wiki << '\t' << kept.size() << '\n';
            wikis++;
            total += kept.size();
        }
        std::cout << total << " terms of " << wikis << " Wiktionaries written to " << termsDir.string() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
