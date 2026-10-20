// Extracts Wikidata entities needed by wiktparse (languages, parts of speech,
// grammatical features, linguistics) from the Wikidata dump into a JSONL file.
#include <algorithm>
#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "WikiData.h"
#include "dump/WikiDump.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// Returns true and writes the entity when needed
bool writeIfNeeded(std::string_view title, const std::string &text, std::ofstream &out) {
    if (!WikiData::mayBeWiktNeeded(title, text))
        return false;
    auto entity = json::parse(text, nullptr, false);
    if (entity.is_discarded() || !WikiData::isWiktNeeded(entity))
        return false;
    out << text << '\n';
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

// Each worker writes its own <stem>_<n><ext>; a worker reads many dump parts
void searchWiktNeeded(const fs::path &output, size_t threads) {
    auto wikiDump = WikiDump::wikidata();
    const size_t workers = wikiDump.workerCount(threads);
    std::cout << wikiDump.path().symbolic_name() << ": " << wikiDump.path().dump_parts().size() << " files, "
              << workers << " threads" << std::endl;
    removeOldWorkerOutputs(output);
    std::vector<std::ofstream> outs;
    for (size_t n = 1; n <= workers; n++)
        outs.push_back(openOutput(workerOutput(output, n).string()));
    std::atomic<size_t> foundCnt{0};
    wikiDump.forEachPageParallel([&](size_t worker, const std::string &title, const std::string &text) {
        if (writeIfNeeded(title, text, outs[worker]))
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
    size_t read = 0, written = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        read++;
        auto entity = json::parse(line, nullptr, false);
        std::string title = entity.is_object() ? entity.value("id", "") : "";
        if (writeIfNeeded(title, line, out))
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

int main(int argc, char *argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    size_t threads = 8;
    if (args.size() >= 2 && args[0] == "-j") {
        threads = std::stoul(args[1]);
        args.erase(args.begin(), args.begin() + 2);
    }
    std::string arg = args.empty() ? "wiktNeeded.jsonl" : args[0];
    if (arg == "-h" || arg == "--help") {
        std::cout << "usage: wiktneeded [-j N] [output.jsonl]    read Wikidata dump in N threads (default 8);\n"
                     "                                          thread n writes output_n.jsonl\n"
                     "       wiktneeded --filter in.jsonl out.jsonl  apply selection to an earlier result\n"
                     "       wiktneeded --first                 write first Wikidata page to firstChuknk\n";
        return 0;
    }
    try {
        if (arg == "--first")
            firstPage();
        else if (arg == "--filter") {
            if (args.size() != 3) throw std::invalid_argument("--filter needs input and output file");
            filterJsonl(args[1], args[2]);
        } else
            searchWiktNeeded(arg, std::max<size_t>(1, threads));
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
