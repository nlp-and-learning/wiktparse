#include "WikiData.h"

#include "Index.h"
#include "WikiFile.h"
#include "WikiName.h"
#include "Xml.h"

void WikiData::testFirstChunkWikiData() {
    WikiName wikiName;
    wikiName.firstWikiDataFile();
    Index index(wikiName);
    index.readIndex();
    WikiFile wikiFile(index);
    wikiFile.open();
    auto chunkStr = wikiFile.decompressChunkByIndex(0);
    auto objects =  allFromXML(chunkStr);
    std::ofstream ofile("firstChuknk");
    for (auto &p : objects) {
        std::cout << p.first << "\n";
        ofile << p.second;
        break;
    }
    wikiFile.close();
}

int WikiData::findSubstrings(const std::string &text, const std::vector<std::string> &listToSearch, const std::vector<std::string> &listNotToSearch) {
    for (int i = 0; i < listNotToSearch.size(); i++) {
        if (text.find(listNotToSearch[i]) != std::string::npos)
            return -1;
    }

    for (int i = 0; i < listToSearch.size(); i++) {
        if (text.find(listToSearch[i]) != std::string::npos)
            return i;
    }
    return -1;
}

void WikiData::search(const std::vector<std::string> &listTosearch, const std::vector<std::string> &listNotTosearch, const std::string &outputFile) {
    WikiName wikiName;
    size_t n = wikiName.initAllWikiData();
    int foundCnt = 0;
    std::ofstream ofile(outputFile);
    if (!ofile) throw std::runtime_error("Cannot open file: " + outputFile);
    for (size_t i=0; i<n; i++) {
        wikiName.setWikiDataFile(i);
        std::cout << wikiName.symbolicName << "  " << wikiName.wikiPath << std::endl;
        Index index(wikiName);
        index.readIndex();
        WikiFile wikiFile(index);
        wikiFile.open();
        size_t onePercent = index.size() / 100;
        for (size_t j=0; j<index.size(); j++) {
            if (j % onePercent  == 0)
                std::cout << j/onePercent << "%" << std::endl;
            auto chunkStr = wikiFile.decompressChunkByIndex(j);
            auto objects =  allFromXML(chunkStr);
            for (auto &p : objects) {
                int found = findSubstrings(p.second, listTosearch, listNotTosearch);
                if (found>=0) {
                    foundCnt++;
                    std::cout << p.first << " " << foundCnt << " " << found << std::endl;
                    ofile << p.second << std::endl;
                }
            }
            // if (j/onePercent >= 10)break;
        }
        wikiFile.close();
        // if (i>=1)break;
    }
    wikiName.closeAllWikiData();
}
