#pragma once
#include <fstream>
#include "AbstractLiner.h"

class TextLiner: public AbstractLiner {
    std::ifstream &infile;
public:
    bool getline(std::string &line) override;
    explicit TextLiner(std::ifstream &infile):infile(infile){}
};
