#pragma once
#include <set>
#include <string>
#include <vector>

std::string trimLeft(const std::string& str);
std::string trimRight(const std::string& str);
std::string trim(const std::string& str);
std::vector<std::string> readLines(const std::string& filename);
void readLines_toSet(const std::string& filename, std::set<std::string>& terms);
std::vector<std::string> split(std::string_view input, char delimiter);
std::string join_from(const std::vector<std::string>& parts, size_t start, char delimiter);