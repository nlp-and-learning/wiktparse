#pragma once
#include <cstdint>
#include <fstream>
#include <vector>

#include "AbstractLiner.h"

struct WikiIndexElem {
    int64_t id;
    std::string title;
};

struct WikiIndexLine {
    int64_t startPos = 0;
    int64_t id = 0;
    std::string title;
    WikiIndexElem elem();
};

struct WikiIndexChunk {
    int64_t startPos = 0;
    int64_t endPos = 0;
    std::vector<WikiIndexElem> elems;
    void clear();
};

class WikiChunker {
    AbstractLiner *liner;
    bool eos = false;
    int64_t eosPos = 0;
    bool start = true;
    WikiIndexLine wikiIndexLine;
    static WikiIndexLine parseLine(const std::string &line);
public :
    WikiChunker(AbstractLiner *liner, int64_t eosPos): liner(liner), eosPos(eosPos) {}
    bool getChunk(WikiIndexChunk &chunk);
};
