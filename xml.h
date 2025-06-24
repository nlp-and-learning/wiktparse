#pragma once
#include <string>
#include <vector>

std::string extractFromXML(const std::string &term, const std::string &chunkStr);
std::vector<std::pair<std::string,std::string>> allFromXML(const std::string &chunkStr);
