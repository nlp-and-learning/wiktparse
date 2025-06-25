#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "textUtils.h"
#include "WikiName.h"

class Index {
public:
    struct IndexedObject {
        size_t chunkIndex;
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
private:
    std::unordered_map<std::string, IndexedObject> objectMap;
    std::vector<long long int> indexVec;
    friend class WikiFile;
    const WikiName &wikiName;
public:
    explicit Index(const WikiName &wikiName): wikiName(wikiName){};
    int readIndex();
    IndexedObject getIndexedObject(const std::string &term) const;
    size_t size() {return indexVec.size()-1;}
};
