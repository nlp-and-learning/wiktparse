#include "tags.h"

#include <iostream>

#include "Index.h"
#include "WikiFile.h"
#include "xml.h"

bool isClear(const std::string &str) {
    return str.find('/') == std::string::npos && str.find(':') == std::string::npos;
}

int count_levelL(const std::string &line) {
    int  pos = 0;
    while (pos<line.size() && line[pos] == '=')
        pos++;
    return pos;
}

int count_levelR(const std::string &line) {
    int level = 0;
    int  pos = line.size()-1;
    while (pos>=0 && line[pos] == '=')
        pos--;
    return line.size() - 1 - pos;
}

std::pair<TitleType,std::string> getTitleType(const std::string &title) {
    if (isClear(title))
        return make_pair(TitleType::Main, title);
    auto pos = title.find("/translations");
    if (pos != std::string::npos) {
        auto partA = title.substr(0, pos);
        auto partB = title.substr(pos + 1);
        if (isClear(partA) && isClear(partB))
            return make_pair(TitleType::Translations, partA);
        else
            return make_pair(TitleType::Other, title);
    }
    pos = title.find(':');
    if (pos != std::string::npos) {
        auto partA = title.substr(0, pos);
        auto partB = title.substr(pos + 1);
        if (partA == "Thesaurus" && isClear(partB))
            return make_pair(TitleType::Thesaurus, partB);
        else
            return make_pair(TitleType::Other, title);
    }
    return make_pair(TitleType::Other, title);
}

void collectAllTags() {
    Index index("en");
    index.readIndex();
    WikiFile wikiFile(index);
    wikiFile.open();
    std::set<std::string> allTags;
    int onePercent = index.size() / 100;
    for (int i=0; i<index.size(); i++) {
        if (i % onePercent  == 0)
            std::cout << i/onePercent << "%" << std::endl;
        auto chunkStr = wikiFile.decompressChunkByIndex(i);
        auto objects =  allFromXML(chunkStr);
        for (auto &p : objects) {
            auto lines = splitLines(p.second);
            for (const auto& line : lines) {
                auto trimmed = trim(line);
                if (trimmed.empty())
                    continue;
                if (trimmed[0] == '=') {
                    if (count_levelL(trimmed)!=count_levelR(trimmed))
                        std::cout << p.first << " : " << trimmed << std::endl;
                    allTags.insert(trimmed);
                }
            }
        }
    }
    wikiFile.close();
    saveToFile(allTags, "alltags.txt");
}

