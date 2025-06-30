#pragma once
#include <string>
#include <vector>

std::string extractFromXML(const std::string &term, const std::string &chunk);
std::vector<std::pair<std::string,std::string>> allFromXML(const std::string &chunk);
