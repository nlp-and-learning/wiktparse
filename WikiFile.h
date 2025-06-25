#pragma once
#include "Index.h"

class WikiFile {
    FILE *file;
    const Index &index;
    const WikiName &wikiName;
public:
    explicit WikiFile(const Index &index);
    std::string decompressChunk(size_t start, int len);
    std::string decompressChunkByIndex(size_t index);
    int open();
    int close();
    std::string extractTerm(std::string term);
    ~WikiFile();
};
