#include "WikiChunker.h"
#include <cstdint>
#include <string>
#include <vector>

WikiIndexElem WikiIndexLine::elem() {
    WikiIndexElem elem;
    elem.id = id;
    elem.title = title;
    return elem;
}

void WikiIndexChunk::clear() {
    startPos = endPos = 0;
    elems.clear();
}

WikiIndexLine WikiChunker::parseLine(const std::string &line) {
    WikiIndexLine indexLine;
    auto pos1 = line.find(':');
    auto pos2 = line.find(':', pos1+1);
    indexLine.startPos = stoi(line);
    indexLine.id = stoi(line.substr(pos1+1, pos2-pos1-1));
    indexLine.title = line.substr(pos2+1);
    return indexLine;
}

bool WikiChunker::getChunk(WikiIndexChunk &chunk) {
    chunk.clear();
    if (eos)
        return false;

    std::string line;
    if (start) {
        if (!liner.getline(line)) {
            eos = true;
            return false;
        }
        wikiIndexLine = parseLine(line);
        start = false;
    }
    chunk.startPos = wikiIndexLine.startPos;
    WikiIndexLine wikiIndexLineLA;
    chunk.elems.push_back(wikiIndexLine.elem());
    while (liner.getline(line)) {
        wikiIndexLineLA = parseLine(line);
        if (wikiIndexLineLA.startPos == wikiIndexLine.startPos) {
            chunk.elems.push_back(wikiIndexLineLA.elem());
        } else {
            wikiIndexLine = wikiIndexLineLA;
            chunk.endPos = wikiIndexLineLA.startPos;
            return true;
        }
    }
    eos = true;
    chunk.endPos = eosPos;
    return true;
}
