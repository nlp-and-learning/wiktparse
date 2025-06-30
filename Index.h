#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "textUtils.h"
#include "WikiName.h"
#include "Bz2Liner.h"

class WikiFile;

class Index {
    FILE *file;
    BZFILE *bzfile;
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
    const WikiFile *wikiFile = nullptr;
    const WikiName &wikiName;

    explicit Index(const WikiName &wikiName): wikiName(wikiName){};
    ~Index();
    int readIndex();
    int open(WikiFile *wikiFile);
    void close();
    bool getChunk(std::string &chunk);
    IndexedObject getIndexedObject(const std::string &term) const;
    size_t size() {return indexVec.size()-1;}
};
