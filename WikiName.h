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
    static std::string wikiNameToIndexName(const fs::path &wikiPath);
    static std::string readDate(const fs::path &dir);
    std::vector<std::pair<std::string, std::string>> fpairs;
public:
    std::string indexPath;
    std::string wikiPath;
    std::string symbolicName;
    void wiktName(const std::string &lang);
    void wikiName(const std::string &lang);
    size_t initAllWikiData();
    void closeAllWikiData();
    void setWikiDataFile(size_t n);
    static void wikiDataList();
    void firstWikiDataFile();
};
