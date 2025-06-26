#pragma once
#include <string>
#include <vector>

class WikiData {
public:
    static void testFirstChunkWikiData();
    static int findSubstrings(const std::string &text, const std::vector<std::string> &listToSearch, const std::vector<std::string> &listNotToSearch);
    static void search(const std::vector<std::string> &listTosearch, const std::vector<std::string> &listNotTosearch, const std::string &outputFile);
};

