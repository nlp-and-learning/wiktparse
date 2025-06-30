#pragma once
#include <string>
#include <vector>

class Xml {
public:
    std::string extractFromXML(const std::string &term, const std::string &chunk);
    std::vector<std::pair<std::string,std::string>> allFromXML(const std::string &chunk);
};

