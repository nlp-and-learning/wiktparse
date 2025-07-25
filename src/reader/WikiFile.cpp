#include "WikiFile.h"

#include <cmath>
#include <iostream>

#include "../liner/Bz2Liner.h"
#include "../xml/Xml.h"

WikiFile::WikiFile(const Index &index): file(nullptr),index(index), wikiName(index.wikiName) {
}

std::string WikiFile::decompressChunk(WikiIndexChunk &chunk) {
    return decompressChunk(chunk.startPos, chunk.endPos-chunk.startPos);
}

std::string WikiFile::decompressChunk(int64_t start, int64_t len) {
    static double max  = 1;
    fseek(file, start, SEEK_SET);
    char *buf = new char[len];
    size_t readed = fread(buf, 1,len, file);

    unsigned int decrypted_len = ceil(len * std::max(max, 15.0));
    char *bufD;
    int decrypted_ret;
    while (true) {
        bufD = new char[decrypted_len+1];
        decrypted_ret = BZ2_bzBuffToBuffDecompress(bufD,
                                                       &decrypted_len,
                                                       buf,
                                                       len,
                                                       0,
                                                       0);
        if (decrypted_ret == BZ_OUTBUFF_FULL)
            delete[] bufD;
        else
            break;
        decrypted_len *= 2;
    }
    max = std::max(max, (double)decrypted_len/len);
    bufD[decrypted_len] = 0;
    std::string result = bufD;
    delete[] bufD;
    delete[] buf;
    return result;
}

std::string WikiFile::decompressChunkByIndex(size_t n) {
    return decompressChunk(index.indexVec[n], index.indexVec[n+1] - index.indexVec[n]);
}

int WikiFile::open() {
    file = fopen(wikiName.wikiPath.c_str(), "rb");
    if (!file) {
        std::cerr << "Could not open file " << wikiName.wikiPath << "\n";
        return 1;
    }
    fseek(file, 0L, SEEK_END);
    m_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    return 0;
}

size_t WikiFile::filePos() {
    return ftell(file);
}

int WikiFile::close() {
    if (file)
        fclose(file);
    file = nullptr;
    return 0;
}

std::string WikiFile::extractTerm(std::string term) {
    auto iobj = index.getIndexedObject(term);
    if (iobj.chunkIndex<0)
        return "";
    auto chunkStr = decompressChunkByIndex(iobj.chunkIndex);
    Xml xml;
    auto termValue = xml.termFromChunk(term, chunkStr);
    return termValue;
}

WikiFile::~WikiFile() {
    close();
}
