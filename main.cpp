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

#include "Index.h"
#include "tags.h"
#include "WikiFile.h"
#include "xml.h"

using namespace std;


string correct_filename(std::string str) {
    std::replace(str.begin(), str.end(), '/', '_');
    return str;
}

int createPages() {
    Index index("en");
    index.readIndex();
    WikiFile wikiFile(index);
    wikiFile.open();
    auto terms_to_extract = readLines("../work/terms_to_extract.txt");
    for (auto term : terms_to_extract) {
        auto termValue = wikiFile.extractTerm(term);
        std::ofstream ofile(correct_filename(term)+".page");
        if (ofile) {
            ofile << termValue;
        } else {
        }
    }
    wikiFile.close();
    return 0;
}


int main() {
    //collectAllTags();
    createPages();
}