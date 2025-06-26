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
#include <sys/stat.h>

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

void createPages() {
    fs::create_directory("../pages");
    vector<string> langs = {"pl","en", "fr", "eo"};
    for (const auto& lang: langs) {
        fs::path directory = "../pages/"+lang;
        fs::create_directory(directory);
        WikiName wikiName;
        wikiName.WiktName(lang);
        Index index(wikiName);
        index.readIndex();
        WikiFile wikiFile(index);
        wikiFile.open();
        auto terms_to_extract = readLines("../work/terms_to_extract." + lang + ".txt");
        for (auto term : terms_to_extract) {
            cout << term << endl;
            auto termValue = wikiFile.extractTerm(term);
            if (!termValue.empty())
                saveToFile(termValue, directory/ (correct_filename(term)+".page"));
        }
        wikiFile.close();
    }
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

void showTitleTypes() {
    const std::regex pattern(R"((.*)wiktionary.*.txt.bz2)");
    fs::path directory("../dump");
    fs::create_directory("titles");
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const std::string filename = entry.path().filename().string();
        std::smatch match;
        if (std::regex_match(filename, match, pattern)) {
            if (match[1].str()=="en")continue;
            cout << match[1].str() << endl;
            WikiName wikiName;
            wikiName.WiktName(match[1].str());
            Index index(wikiName);
            index.readIndex();
            map<string, set<string>> setMap;
            set<string> allTitles;
            for (const auto& title : index.objectMap | std::views::keys) {
                if (title.find('/')==string::npos && title.find(':')==string::npos) continue;
                allTitles.insert(title);
                if (title.find('/')!=string::npos) {
                    auto parts = split(title, '/');
                    for (int i=0; i<parts.size(); i++) {
                        string key = format("s{}_{}", i, parts.size());
                        setMap[key].insert(parts[i]);
                    }
                } else if (title.find(':')!=string::npos) {
                    auto parts = split(title, ':');
                    for (int i=0; i<parts.size(); i++) {
                        string key = format("c{}_{}", i, parts.size());
                        setMap[key].insert(parts[i]);
                    }
                }
            }
            auto outname = "titles/" + match[1].str() + ".all.txt";;
            saveToFile(allTitles, outname);
            for (const auto& pair : setMap) {
                const std::string& key = pair.first;
                // if (key!="c0_2" && key!="s1_2") continue;
                outname = "titles/" + match[1].str() + "." + pair.first + ".txt";
                saveToFile(pair.second, outname);
            }
        }
    }
}

int main() {
    createPages();
}