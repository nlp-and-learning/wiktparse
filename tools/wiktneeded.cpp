// Extracts Wikidata entities needed by wiktparse (languages, parts of speech,
// grammatical features, linguistics) from the Wikidata dump into a JSONL file.
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "WikiData.h"
#include "dump/WikiDump.h"

using json = nlohmann::json;

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

void searchWiktNeeded(const std::string &outputFile) {
    auto out = openOutput(outputFile);
    auto wikiDump = WikiDump::wikidata();
    std::cout << wikiDump.path().symbolic_name() << std::endl;
    size_t foundCnt = 0;
    wikiDump.forEachPage([&](const std::string &title, const std::string &text) {
        if (writeIfNeeded(title, text, out))
            foundCnt++;
        return true;
    });
    std::cout << foundCnt << " entities written to " << outputFile << std::endl;
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
    std::string arg = argc > 1 ? argv[1] : "wiktNeeded.jsonl";
    if (arg == "-h" || arg == "--help") {
        std::cout << "usage: wiktneeded [output.jsonl]          read Wikidata dump (default wiktNeeded.jsonl)\n"
                     "       wiktneeded --filter in.jsonl out.jsonl  apply selection to an earlier result\n"
                     "       wiktneeded --first                 write first Wikidata page to firstChuknk\n";
        return 0;
    }
    try {
        if (arg == "--first")
            firstPage();
        else if (arg == "--filter") {
            if (argc != 4) throw std::invalid_argument("--filter needs input and output file");
            filterJsonl(argv[2], argv[3]);
        } else
            searchWiktNeeded(arg);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
