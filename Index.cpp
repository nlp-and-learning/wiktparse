#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include "Index.h"

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
    Bz2Liner bz2_liner(BufLen, bzfile);
    struct stat statBuf{};
    stat(wikiName.wikiPath.c_str(), &statBuf);
    WikiIndexChunk chunk;
    chunker = new WikiChunker(bz2_liner, statBuf.st_size);
    while (chunker->getChunk(chunk)) {
        indexVec.push_back(chunk.startPos);
        if (indexVec.size() % 10000 == 0)
            std::cout << "." << std::flush;
    }
    indexVec.push_back(chunk.endPos);
    delete chunker;
    chunker = nullptr;
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
    delete chunker;
    chunker = nullptr;
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
