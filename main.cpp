#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>
#include <bzlib.h>
#include <map>
#include <set>
#include <sstream>
#include <ranges>
#include <string_view>

#include "Comments.h"
#include "Index.h"
#include "tags.h"
#include "WikiData.h"
#include "WikiFile.h"
#include "xml.h"

using namespace std;


string correct_filename(std::string str) {
    std::replace(str.begin(), str.end(), '/', '_');
    return str;
}

int createPages() {
    WikiName wikiName;
    wikiName.WiktName("en");
    Index index(wikiName);
    index.readIndex();
    WikiFile wikiFile(index);
    wikiFile.open();
    auto terms_to_extract = readLines("../work/terms_to_extract.txt");
    for (auto term : terms_to_extract) {
        auto termValue = wikiFile.extractTerm(term);
        saveToFile(termValue, correct_filename(term)+".page");
    }
    wikiFile.close();
    return 0;
}

void searchWikiDataWiktNeeded() {
    vector<string> listTosearch;
    listTosearch.emplace_back("\"P220\"");//ISO 639-1 code
    listTosearch.emplace_back("\"Q82042\"");//part of speech
    listTosearch.emplace_back("\"Q8162\"");//linguistics , Etymology
    listTosearch.emplace_back("\"Q34770\"");//language
    vector<string> listNotTosearch;
    listNotTosearch.emplace_back("\"Q5\"");
    WikiData::search(listTosearch,listNotTosearch, "wiktNeeded.jsonl");
}

int main() {
    searchWikiDataWiktNeeded();
}