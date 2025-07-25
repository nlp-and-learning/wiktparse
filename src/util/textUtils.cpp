#include "textUtils.h"
#include <fstream>
#include <sstream>
#include <ranges>

std::string trimLeft(const std::string& str)
{
    const auto strBegin = str.find_first_not_of(" \t");
    if (strBegin == std::string::npos)
        return ""; // no content
    return str.substr(strBegin, str.length() - strBegin);
}


std::string trimRight(const std::string& str)
{
    const auto strEnd = str.find_last_not_of(" \t\r\n");
    if (strEnd == std::string::npos)
        return ""; // no content
    return str.substr(0, strEnd + 1);
}

std::string trim(const std::string& str) {
    return trimLeft(trimRight(str));
}

std::vector<std::string> readLines(const std::string& filename) {
    std::vector<std::string> lines;
    std::ifstream infile(filename);
    for( std::string line; getline( infile, line ); )
        lines.push_back(trimRight(line));
    return lines;
}

std::set<std::string> readLinesToSet(const std::string& filename) {
    std::ifstream in(filename);
    if (!in)
        throw std::runtime_error("Cannot open file: " + filename);

    std::set<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty())
            lines.insert(line);
    }
    return lines;
}

std::vector<std::string> split(std::string_view input, char delimiter) {
    std::vector<std::string> result;
    for (auto part : input | std::views::split(delimiter)) {
        result.emplace_back(part.begin(), part.end());
    }
    return result;
}

std::vector<std::string> splitLines(std::string_view input) {
    return split(input, '\n');
}


std::string joinFrom(const std::vector<std::string>& parts, size_t start, char delimiter) {
    std::ostringstream oss;
    bool first = true;

    for (const auto& part : parts | std::views::drop(start)) {
        if (!first) oss << delimiter;
        oss << part;
        first = false;
    }

    return oss.str();
}

std::string readFileToString(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) throw std::runtime_error("Cannot open file: " + filename);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void saveToFile(const std::string &text, const std::string &filename) {
    std::ofstream ofile(filename);
    if (!ofile) throw std::runtime_error("Cannot open file: " + filename);
    ofile << text;
}
