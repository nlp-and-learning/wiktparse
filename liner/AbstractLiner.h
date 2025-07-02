#pragma once
#include <string>

class AbstractLiner {
public:
    virtual ~AbstractLiner() = default;
    virtual bool getline(std::string &line) = 0;
};
