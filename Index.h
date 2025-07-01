#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "textUtils.h"
#include "WikiName.h"
#include "Bz2Liner.h"
#include "WikiChunker.h"

class Index {
    FILE *file = nullptr;
    BZFILE *bzfile = nullptr;
    Bz2Liner * bz2_liner = nullptr;
    WikiChunker * chunker = nullptr;
public:
    struct IndexedObject {
        int chunkIndex;
        std::string id;
        std::string title;
        IndexedObject() {
            chunkIndex = 0;
        }
        explicit IndexedObject(const std::string &line, size_t chunkIndex):chunkIndex(chunkIndex) {
            auto parts = split(trim(line), ':');
            id = trim(parts[1]);
            title = join_from(parts, 2, ':');
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
