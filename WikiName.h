#pragma once
#include <iostream>
#include <filesystem>
#include <regex>
#include <vector>
#include <string>
#include <algorithm>
#include <format>

#include "textUtils.h"

namespace fs = std::filesystem;



class WikiName {
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
    std::string wiktPath;
    void WiktName(const std::string &lang) {
        std::string date = readDate();
        indexPath = "../dump/" + lang + "wiktionary-"+ date+ "-pages-articles-multistream-index.txt.bz2";
        wiktPath= "../dump/" + lang + "wiktionary-"+ date+ "-pages-articles-multistream.xml.bz2";
    }
    static void WikiDataList() {
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
            out << std::format("{:03}", number) << "  " << filename.string() << "\n";
            number++;
        }
    }
};
