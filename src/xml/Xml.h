#pragma once
#include <string>
#include <vector>

class Xml {
    bool cleanComments;
public:
    explicit Xml(bool cleanComments = true) : cleanComments(cleanComments) {}
    std::string termFromChunk(const std::string &term, const std::string &chunk);
    std::vector<std::pair<std::string,std::string>> allFromChunk(const std::string &chunk);
};

