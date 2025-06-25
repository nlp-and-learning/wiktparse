#pragma once
#include <fstream>
#include <string>

#include "textUtils.h"

class WikiName {
    static std::string readDate() {
        std::ifstream file("../dump/DATE");
        std::string date;

        if (file) {
            std::getline(file, date);
        } else
            throw std::runtime_error("run dump.run.sh");

        return trim(date);
    }
public:
    std::string indexPath;
    std::string wiktPath;
    void WiktName(const std::string &lang) {
        std::string date = readDate();
        indexPath = "../dump/" + lang + "wiktionary-"+ date+ "-pages-articles-multistream-index.txt.bz2";
        wiktPath= "../dump/" + lang + "wiktionary-"+ date+ "-pages-articles-multistream.xml.bz2";
    }
};
