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

// Writes the entity when needed; entities found only by a header term are
// also logged (id, language, term) to termLog. Returns true when written.
bool writeIfNeeded(std::string_view title, const std::string &text, std::ofstream &out, std::ofstream *termLog) {
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
    for (size_t i = 0; i + 1 < args.size();) {
        if (args[i] == "-j") {
            threads = std::max<size_t>(1, std::stoul(args[i + 1]));
        } else if (args[i] == "--terms") {
            termsDir = args[i + 1];
            termsGiven = true;
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
                     "           read Wikidata dump in N threads (default 8); thread n writes output_n.jsonl;\n"
                     "           with term lists (default ./terms if present, from wiktterms) also entities\n"
                     "           whose label is a header term in N languages (default 2); these are\n"
                     "           logged in output_terms_n.tsv\n"
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
