#include "WikiData.h"

#include "Index.h"
#include "WikiFile.h"
#include "WikiName.h"
#include "xml.h"

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

void WikiData::search(const std::vector<std::string> &listTosearch, const std::string &outputFIle) {
    /*WikiName wikiName;
    int n = wikiName.initAllWikiData();
    for (int i=0; i<n; i++)
        wikiName.setWIkiDAtaFile(i)
    wikiName.closeAllWikiData();*/
}
