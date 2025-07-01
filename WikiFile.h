#pragma once
#include "Index.h"

class WikiFile {
    FILE *file;
    const Index &index;
    const WikiName &wikiName;
    int64_t m_size = 0;
public:
    explicit WikiFile(const Index &index);
    std::string decompressChunk(WikiIndexChunk &chunk);
    std::string decompressChunk(int64_t start, int64_t len);
    std::string decompressChunkByIndex(size_t index);
    int open();
    int close();
    [[nodiscard]] int64_t size() const {return m_size;}
    int64_t filePos();
    std::string extractTerm(std::string term);
    ~WikiFile();
};
