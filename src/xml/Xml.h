#pragma once
#include <string>
#include <vector>
#include "old2_Comments.h"

class Xml {
    old2_Comments comments;
    bool cleanComments;
public:
    explicit Xml(bool cleanComments = true) : cleanComments(cleanComments) {}
    std::string termFromChunk(const std::string &term, const std::string &chunk);
    std::vector<std::pair<std::string,std::string>> allFromChunk(const std::string &chunk);
};

