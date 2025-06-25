#pragma once
#include <iostream>
#include <filesystem>
#include <regex>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <format>

#include "textUtils.h"

namespace fs = std::filesystem;

class WikiName {
    static std::string wikiNameToIndexName(const fs::path &wikiPath) {
        auto dir = wikiPath.parent_path();     // ../dumpWD
        auto wikiName = wikiPath.filename().string();
        auto pos = wikiName.find("multistream");
        if (pos == std::string::npos) {
            throw std::runtime_error("No 'multistream' in filename");
        }

        std::string indexFile = wikiName;
        indexFile.insert(pos + std::strlen("multistream"), "-index");

        // Replace extension from ".xml" to ".txt"
        auto dotPos = indexFile.rfind(".xml");
        if (dotPos == std::string::npos) {
            throw std::runtime_error("No externsion .xml in filename");
        }

        indexFile.replace(dotPos, 4, ".txt");
        return dir/indexFile;
    }

    static std::string readDate() {
        std::ifstream file("../dump/DATE");
        std::string date;

        if (file) {
            std::getline(file, date);
        } else
            throw std::runtime_error("run dump.run.sh");

        return trim(date);
    }
public:
    std::string indexPath;
    std::string wikiPath;
    void WiktName(const std::string &lang) {
        std::string date = readDate();
        indexPath = "../dump/" + lang + "wiktionary-"+ date+ "-pages-articles-multistream-index.txt.bz2";
        wikiPath= "../dump/" + lang + "wiktionary-"+ date+ "-pages-articles-multistream.xml.bz2";
        auto indexPath2 = wikiNameToIndexName(wikiPath);
        std::cout << indexPath << std::endl;
        std::cout << indexPath2 << std::endl;
    }

    static void wikiDataList() {
        const std::regex pattern(
        R"(wikidatawiki-.*-pages-articles-multistream.*\.xml-p(\d+)p\d+\.bz2$)");

        fs::path directory("../dumpWD");

        std::vector<std::pair<uint64_t, fs::path>> matched_files;

        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;

            const std::string filename = entry.path().filename().string();

            std::smatch match;

            if (std::regex_match(filename, match, pattern)) {
                try {
                    uint64_t number = std::stoull(match[1].str());
                    matched_files.emplace_back(number, filename);
                } catch (const std::exception& e) {
                    std::cerr << "Błąd parsowania liczby w pliku: " << entry.path() << "\n";
                }

            }
        }

        std::sort(matched_files.begin(), matched_files.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });

        std::ofstream out(directory/"list.txt");  // otwórz plik do zapisu
        int number = 0;
        for (const auto& [num, filename] : matched_files) {
            out << std::format("{:03}", number) << " " << filename.string() << "\n";
            number++;
        }
    }

    void firstWikiDataFile() {
        fs::path directory("../dumpWD");
        fs::path listFile = directory / "list.txt";

        if (!fs::exists(listFile)) {
            wikiDataList();
        }

        std::ifstream in(listFile);
        if (!in) {
            throw std::runtime_error("can't crate list.txt");
        }

        std::string line;
        if (!std::getline(in, line)) {
            throw std::runtime_error("list.txt is empty");
        }

        std::vector<std::string> tokens = split(trim(line), ' ');
        std::string wikiName = tokens[1];
        wikiPath= directory/ wikiName;
        indexPath = directory/ wikiNameToIndexName(wikiName);
    }
};
