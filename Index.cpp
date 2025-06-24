#include <sys/stat.h>
#include <bzlib.h>
#include <fstream>
#include <iostream>
#include "Index.h"
#include "Bz2Liner.h"
#include "textUtils.h"

Index::Index(const std::string &lang) {
    std::string date = readDate();
    indexPath = "../dump/" + lang + "wiktionary-"+ date+ "-pages-articles-multistream-index.txt.bz2";
    wiktPath= "../dump/" + lang + "wiktionary-"+ date+ "-pages-articles-multistream.xml.bz2";
}

int Index::readIndex() {
    FILE *file = fopen(indexPath.c_str(), "rb");
    if (!file) {
        std::cerr << "Could not open file " << indexPath << "\n";
        return 1;
    }

    BZFILE *bzfile = BZ2_bzReadOpen(nullptr, file, 0, 0, nullptr, 0);
    if (!bzfile) {
        std::cerr << "Could not open bz2 file\n";
        return 1;
    }

    const int BufLen = 512 * 1024;
    Bz2Liner sl(BufLen, bzfile);
    long long pn = -1;
    std::set<std::string> terms_to_exrtract;

    for (std::string line; sl.getline(line);) {
        long long n = stoll(line);
        if (n != pn) {
            indexVec.push_back(n);
            if (indexVec.size() % 10000 == 0)
                std::cout << "." << std::flush;
            pn = n;
        }
        IndexedObject indexedObject(line, indexVec.size()-1);
        objectMap[indexedObject.title] = indexedObject;
    }
    struct stat statBuf{};
    stat(wiktPath.c_str(), &statBuf);
    indexVec.push_back(statBuf.st_size);
    int bzerror;
    BZ2_bzReadClose(&bzerror, bzfile);
    fclose(file);
    return 0;
}

Index::IndexedObject Index::getIndexedObject(const std::string &term) const{
    return objectMap.at(term);
}

std::string Index::readDate() {
    std::ifstream file("../dump/DATE");
    std::string date;

    if (file) {
        std::getline(file, date);
    } else
        throw std::runtime_error("run dump.run.sh");

    return trim(date);
}
