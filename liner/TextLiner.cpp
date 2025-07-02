#include "TextLiner.h"

bool TextLiner::getline(std::string &line) {
    //!! = explicit cast of std::istream& to bool (using the good() method or implicit conversion operator)
    return !!std::getline(infile, line);
}
