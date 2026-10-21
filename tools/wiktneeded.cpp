// Extracts Wikidata entities needed by wiktparse (languages, parts of speech,
// grammatical features, linguistics) from the Wikidata dump into a JSONL file.
// With term lists of Wiktionary headers (wiktterms) also entities whose label
// or alias in the language of a Wiktionary equals one of its header terms.
#include <algorithm>
#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "WikiData.h"
#include "WikiTerms.h"
#include "dump/WikiDump.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

WikiTerms terms;
size_t minTermLanguages = 2; // label must be a header term in so many languages
bool removeHomonymTerms = true;
std::set<std::string, std::less<>> items; // --items: Q ids always selected

// Entity selected by claims or listed in --items
bool selected(const json &entity) {
    return items.contains(entity.value("id", "")) || WikiData::isWiktNeeded(entity);
}

// Writes the entity when needed; entities found only by a header term are
// also logged (id, language, term) to termLog. Returns true when written.
bool writeIfNeeded(std::string_view title, const std::string &text, std::ofstream &out, std::ofstream *termLog) {
    // Listed items, but not redirect pages of merged items ({"entity":..,"redirect":..})
    if (items.contains(title) && text.find("\"redirect\":") == std::string::npos) {
        out << text << '\n';
        return true;
    }
    bool byClaims = WikiData::mayBeWiktNeeded(title, text);
    bool byTerm = !terms.empty() && terms.mayMatch(text);
    if (!byClaims && !byTerm)
        return false;
    auto entity = json::parse(text, nullptr, false);
    if (entity.is_discarded())
        return false;
    if (byClaims && WikiData::isWiktNeeded(entity)) {
        out << text << '\n';
        return true;
    }
    if (!byTerm || WikiData::isExcludedTermMatch(entity))
        return false;
    auto matched = terms.matches(entity);
    // A linguistic concept is a header in many Wiktionaries; a homonym (island,
    // ethnic group, surname) usually matches the headers of one only
    std::set<std::string> languages;
    for (const auto &[language, label] : matched)
        languages.insert(language);
    if (languages.size() < minTermLanguages)
        return false;
    out << text << '\n';
    if (termLog) {
        *termLog << entity.value("id", "");
        for (size_t i = 0; i < std::min<size_t>(matched.size(), 5); i++)
            *termLog << '\t' << matched[i].first << ':' << matched[i].second;
        *termLog << '\n';
    }
    return true;
}

std::ofstream openOutput(const std::string &outputFile) {
    std::ofstream out(outputFile);
    if (!out) throw std::runtime_error("Cannot open file: " + outputFile);
    return out;
}

// Output of worker n (1-based): <stem>_<n><ext>, e.g. wiktNeeded_1.jsonl
fs::path workerOutput(const fs::path &output, size_t n) {
    return output.parent_path() / (output.stem().string() + "_" + std::to_string(n) + output.extension().string());
}

// Removes <stem>_<n><ext> files of an earlier run, which could have more workers
void removeOldWorkerOutputs(const fs::path &output) {
    const std::regex pattern(output.stem().string() + R"(_\d+)" + std::regex_replace(
        output.extension().string(), std::regex(R"(\.)"), R"(\.)"));
    auto dir = output.parent_path().empty() ? fs::path(".") : output.parent_path();
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && std::regex_match(entry.path().filename().string(), pattern))
            fs::remove(entry.path());
    }
}

// Log of term matches next to the output: wiktNeeded.jsonl -> wiktNeeded_terms.tsv
fs::path termLogPath(const fs::path &output) {
    return output.parent_path() / (output.stem().string() + "_terms.tsv");
}

// Second pass over written outputs: an entity selected only by header terms is a
// homonym when its terms, except those already labels of entities selected by
// claims ("root" of a plant besides the linguistic root), are in fewer than
// minTermLanguages languages. Rewrites outputs and term logs without homonyms.
void removeHomonyms(const std::vector<fs::path> &outputs, const std::vector<fs::path> &termLogs) {
    std::set<std::pair<std::string, std::string>> covered; // (language, term) of claims-selected entities
    for (const auto &file : outputs) {
        std::ifstream in(file);
        for (std::string line; std::getline(in, line);) {
            auto entity = json::parse(line, nullptr, false);
            if (!entity.is_discarded() && selected(entity))
                for (auto &key : terms.matchedTerms(entity))
                    covered.insert(std::move(key));
        }
    }
    std::set<std::string> removed;
    for (const auto &file : outputs) {
        auto tmp = fs::path(file.string() + ".tmp");
        {
            std::ifstream in(file);
            auto out = openOutput(tmp.string());
            for (std::string line; std::getline(in, line);) {
                auto entity = json::parse(line, nullptr, false);
                if (!entity.is_discarded() && !selected(entity)) {
                    std::set<std::string> languages;
                    for (const auto &key : terms.matchedTerms(entity))
                        if (!covered.contains(key))
                            languages.insert(key.first);
                    if (languages.size() < minTermLanguages) {
                        removed.insert(entity.value("id", ""));
                        continue;
                    }
                }
                out << line << '\n';
            }
        }
        fs::rename(tmp, file);
    }
    for (const auto &file : termLogs) {
        auto tmp = fs::path(file.string() + ".tmp");
        {
            std::ifstream in(file);
            auto out = openOutput(tmp.string());
            for (std::string line; std::getline(in, line);)
                if (!removed.contains(line.substr(0, line.find('\t'))))
                    out << line << '\n';
        }
        fs::rename(tmp, file);
    }
    std::cout << removed.size() << " homonyms of claims-selected terms removed" << std::endl;
}

// Each worker writes its own <stem>_<n><ext>; a worker reads many dump parts
void searchWiktNeeded(const fs::path &output, size_t threads) {
    auto wikiDump = WikiDump::wikidata();
    const size_t workers = wikiDump.workerCount(threads);
    std::cout << wikiDump.path().symbolic_name() << ": " << wikiDump.path().dump_parts().size() << " files, "
              << workers << " threads" << std::endl;
    auto termLogs = termLogPath(output);
    removeOldWorkerOutputs(output);
    removeOldWorkerOutputs(termLogs);
    std::vector<std::ofstream> outs, logs;
    for (size_t n = 1; n <= workers; n++) {
        outs.push_back(openOutput(workerOutput(output, n).string()));
        if (!terms.empty())
            logs.push_back(openOutput(workerOutput(termLogs, n).string()));
    }
    std::atomic<size_t> foundCnt{0};
    wikiDump.forEachPageParallel([&](size_t worker, const std::string &title, const std::string &text) {
        if (writeIfNeeded(title, text, outs[worker], logs.empty() ? nullptr : &logs[worker]))
            foundCnt++;
    }, threads);
    std::cout << foundCnt << " entities written to " << workerOutput(output, 1).string() << " .. "
              << workerOutput(output, workers).filename().string() << std::endl;
    if (!terms.empty() && removeHomonymTerms) {
        outs.clear();
        logs.clear();
        std::vector<fs::path> outputFiles, logFiles;
        for (size_t n = 1; n <= workers; n++) {
            outputFiles.push_back(workerOutput(output, n));
            logFiles.push_back(workerOutput(termLogs, n));
        }
        removeHomonyms(outputFiles, logFiles);
    }
}

// Applies current selection to an earlier result, without the dump
void filterJsonl(const std::string &inputFile, const std::string &outputFile) {
    std::ifstream in(inputFile);
    if (!in) throw std::runtime_error("Cannot open file: " + inputFile);
    auto out = openOutput(outputFile);
    std::ofstream log;
    if (!terms.empty())
        log = openOutput(termLogPath(outputFile).string());
    size_t read = 0, written = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        read++;
        auto entity = json::parse(line, nullptr, false);
        std::string title = entity.is_object() ? entity.value("id", "") : "";
        if (writeIfNeeded(title, line, out, terms.empty() ? nullptr : &log))
            written++;
    }
    std::cout << written << " of " << read << " entities written to " << outputFile << std::endl;
    if (!terms.empty() && removeHomonymTerms) {
        out.close();
        log.close();
        removeHomonyms({outputFile}, {termLogPath(outputFile)});
    }
}

void firstPage() {
    auto wikiDump = WikiDump::wikidata();
    std::ofstream ofile("firstChuknk");
    wikiDump.forEachPage([&](const std::string &title, const std::string &text) {
        std::cout << title << "\n";
        ofile << text;
        return false;
    });
}

} // namespace

int main(int argc, char *argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    size_t threads = 8;
    fs::path termsDir = "terms";
    bool termsGiven = false;
    fs::path rulesFile = WikiData::defaultRulesPath();
    fs::path itemsFile = "wiktlangs_qid.txt"; // Q ids of English Wiktionary languages (wiktlangs)
    bool itemsGiven = false;
    if (auto flag = std::ranges::find(args, "--keep-homonyms"); flag != args.end()) {
        removeHomonymTerms = false;
        args.erase(flag);
    }
    for (size_t i = 0; i + 1 < args.size();) {
        if (args[i] == "-j") {
            threads = std::max<size_t>(1, std::stoul(args[i + 1]));
        } else if (args[i] == "--terms") {
            termsDir = args[i + 1];
            termsGiven = true;
        } else if (args[i] == "--items") {
            itemsFile = args[i + 1];
            itemsGiven = true;
        } else if (args[i] == "--rules") {
            rulesFile = args[i + 1];
        } else if (args[i] == "--min-term-languages") {
            minTermLanguages = std::max<size_t>(1, std::stoul(args[i + 1]));
        } else {
            i++;
            continue;
        }
        args.erase(args.begin() + i, args.begin() + i + 2);
    }
    std::string arg = args.empty() ? "wiktNeeded.jsonl" : args[0];
    if (arg == "-h" || arg == "--help") {
        std::cout << "usage: wiktneeded [-j N] [--rules FILE] [--terms DIR] [--min-term-languages N] [output.jsonl]\n"
                     "           rules: ids of classes and properties (default res/wikidata_selection.tsv)\n"
                     "           --items FILE: Q ids always selected, one per line\n"
                     "           (default ./wiktlangs_qid.txt if present, written by wiktlangs)\n"
                     "           read Wikidata dump in N threads (default 8); thread n writes output_n.jsonl;\n"
                     "           with term lists (default ./terms if present, from wiktterms) also entities\n"
                     "           whose label is a header term in N languages (default 2); these are\n"
                     "           logged in output_terms_n.tsv; afterwards homonyms of terms of\n"
                     "           claims-selected entities are removed (--keep-homonyms to keep them)\n"
                     "       wiktneeded [--terms DIR] --filter in.jsonl out.jsonl\n"
                     "           apply selection to an earlier result\n"
                     "       wiktneeded --first   write first Wikidata page to firstChuknk\n";
        return 0;
    }
    try {
        WikiData::loadRules(rulesFile);
        std::cout << "rules " << rulesFile.string() << std::endl;
        if (termsGiven || fs::is_directory(termsDir)) {
            auto count = terms.load(termsDir);
            std::cout << count << " header terms loaded from " << termsDir.string() << std::endl;
        }
        if (itemsGiven || fs::exists(itemsFile)) {
            std::ifstream in(itemsFile);
            if (!in) throw std::runtime_error("Cannot open " + itemsFile.string());
            for (std::string line; std::getline(in, line);)
                if (!line.empty() && line[0] == 'Q')
                    items.insert(line.substr(0, line.find_first_of(" \t\r")));
            std::cout << items.size() << " items always selected, from " << itemsFile.string() << std::endl;
        }
        if (arg == "--first")
            firstPage();
        else if (arg == "--filter") {
            if (args.size() != 3) throw std::invalid_argument("--filter needs input and output file");
            filterJsonl(args[1], args[2]);
        } else
            searchWiktNeeded(arg, threads);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
