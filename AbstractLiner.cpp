#include <cassert>
#include "AbstractLiner.h"

using namespace std;

AbstractLiner::AbstractLiner(int bufLen) : bufLen(bufLen) {
    buffer = new char[bufLen];
}

AbstractLiner::~AbstractLiner() {
    delete buffer;
}

bool AbstractLiner::getline(std::string &line) {
    if (!initialized) {
        readBuffer();
        initialized = true;
    }
    if (gcount == 0)
        return false;
    if (startLine == gcount || eolPos == gcount)
        return false;
    findNextEol();
    if (gcount == 0) {
        line = result;
        return true;
    }
    string_view sv(buffer + startLine, eolPos - startLine);
    result += sv;
    if (eolPos < gcount)
        setStartAfterEol();
    line = result;
    result = "";
    return true;
}

bool AbstractLiner::findEolOne() {
    eolPos = gcount;
    for (int i = startLine; i < gcount; i++) {
        if (isEol(buffer[i])) {
            eolPos = i;
            return true;
        }
    }
    return false;
}

void AbstractLiner::findNextEol() {
    while (!findEolOne() && gcount == bufLen) {
        assert(eolPos == gcount);
        string_view sv(buffer + startLine, eolPos - startLine);
        result += sv;
        readBuffer();
        startLine = 0;
    }
}

void AbstractLiner::setStartAfterEol() {
    assert(eolPos < gcount);
    startLine = eolPos;
    if (buffer[startLine] == '\n')
        skipEol();
    else {
        assert(buffer[startLine] == '\r');
        skipEol();
        if (startLine == gcount)
            return;
        if (buffer[startLine] == '\n')
            skipEol();
    }
}

void AbstractLiner::skipEol() {
    startLine++;
    assert(gcount > 0);
    if (startLine == gcount && !eof) {
        readBuffer();
        startLine = 0;
    }
}

bool AbstractLiner::isEol(char c) {
    return c == '\n' || c == '\r';
}
