#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../util/textUtils.h"
#include "WikiName.h"
#include "../liner/Bz2Liner.h"
#include "../WikiChunker.h"

class Index {
    FILE *file = nullptr;
    BZFILE *bzfile = nullptr;
    Bz2Liner * bz2_liner = nullptr;
    WikiChunker * chunker = nullptr;
public:
    struct IndexedObject {
        int64_t id;
        std::string title;
        int chunkIndex;
        IndexedObject() {
            chunkIndex = 0;
        }
        explicit IndexedObject(const WikiIndexElem &indexElem, size_t chunkIndex):chunkIndex(chunkIndex) {
            id = indexElem.id;
            title = indexElem.title;
        }
    };
    std::unordered_map<std::string, IndexedObject> objectMap;
    std::vector<long long int> indexVec;
    const WikiName &wikiName;

    explicit Index(const WikiName &wikiName): wikiName(wikiName){};
    ~Index();
    int readIndex();
    int open();
    void close();
    bool getChunk(WikiIndexChunk &chunk);
    IndexedObject getIndexedObject(const std::string &term) const;
    size_t size() {return indexVec.size()-1;}
};
