#include "headers.h"

int count_levelL(const std::string &line) {
    int  pos = 0;
    while (pos<line.size() && line[pos] == '=')
        pos++;
    return pos;
}

int count_levelR(const std::string &line) {
    int level = 0;
    int  pos = line.size()-1;
    while (pos>=0 && line[pos] == '=')
        pos--;
    return line.size() - 1 - pos;
}

std::string trim_header(const std::string& line) {
    int countL = count_levelL(line);
    int countR = count_levelR(line);
    return line.substr(countL, line.size() - countR - countL);
}
