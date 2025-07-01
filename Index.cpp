#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include "Index.h"
#include "textUtils.h"

Index::~Index() {
    close();
}

int Index::readIndex() {
    file = fopen(wikiName.indexPath.c_str(), "rb");
    if (!file) {
        std::cerr << "Could not open file " << wikiName.indexPath << "\n";
        return 1;
    }

    bzfile = BZ2_bzReadOpen(nullptr, file, 0, 0, nullptr, 0);
    if (!bzfile) {
        std::cerr << "Could not open bz2 file\n";
        fclose(file);
        return 1;
    }

    const int BufLen = 512 * 1024;
    Bz2Liner sl(BufLen, bzfile);
    long long pn = -1;
    std::set<std::string> terms_to_extract;

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
    stat(wikiName.wikiPath.c_str(), &statBuf);
    indexVec.push_back(statBuf.st_size);
    int bzerror;
    BZ2_bzReadClose(&bzerror, bzfile);
    bzfile = nullptr;
    fclose(file);
    file = nullptr;
    return 0;
}

int Index::open(WikiFile *wikiFile) {
    this->wikiFile = wikiFile;
    FILE *file = fopen(wikiName.indexPath.c_str(), "rb");
    if (!file) {
        std::cerr << "Could not open file " << wikiName.indexPath << "\n";
        return 1;
    }

    BZFILE *bzfile = BZ2_bzReadOpen(nullptr, file, 0, 0, nullptr, 0);
    if (!bzfile) {
        std::cerr << "Could not open bz2 file\n";
        fclose(file);
        return 1;
    }
    return 0;
}

void Index::close() {
    if (bzfile) {
        int bzerror;
        BZ2_bzReadClose(&bzerror, bzfile);
        bzfile = nullptr;
    }
    if (file) {
        fclose(file);
        file = nullptr;
    }
}

bool Index::getChunk(std::string &chunk) {
    return false;
}

Index::IndexedObject Index::getIndexedObject(const std::string &term) const{
    if (objectMap.contains(term))
        return objectMap.at(term);
    else {
        Index::IndexedObject obj;
        obj.chunkIndex = -1;
        obj.id = "";
        obj.title = term;
        return obj;
    }
}
