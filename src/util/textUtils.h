#pragma once
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

std::string trimLeft(const std::string& str);
std::string trimRight(const std::string& str);
std::string trim(const std::string& str);
std::vector<std::string> readLines(const std::string& filename);
std::set<std::string> readLinesToSet(const std::string& filename);
std::vector<std::string> split(std::string_view input, char delimiter);
std::vector<std::string> splitLines(std::string_view input);
std::string joinFrom(const std::vector<std::string>& parts, size_t start, char delimiter);
std::string readFileToString(const std::string& filename);
void saveToFile(const std::string &text, const std::string &filename);

template<typename Container>
void saveToFile(const Container& lines, const std::string& filename) {
    std::ofstream out(filename);
    if (!out) throw std::runtime_error("Cannot open file: " + filename);
    for (const auto& line : lines)
        out << line << '\n';
}