#pragma once
#include <string>
#include "AbstractLiner.h"

class AbstractBufferedLiner: public AbstractLiner {
    bool findEolOne();
    void findNextEol();
    void setStartAfterEol();
    int eolPos = 0;
    int startLine = 0;
    std::string result;
    void skipEol();
    static bool isEol(char c);
protected:
    bool initialized = false;
    char *buffer;
    int bufLen;
    int gcount = 0;
    bool eof = false;
    virtual void readBuffer() = 0;
public:
    bool getline(std::string &line) override;
    explicit AbstractBufferedLiner(int bufLen);
    ~AbstractBufferedLiner() override;
};
