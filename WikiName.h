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
    static std::string readDate();
public:
    std::string indexPath;
    std::string wikiPath;
    void WiktName(const std::string &lang);
    static void wikiDataList();
    void firstWikiDataFile();
};
