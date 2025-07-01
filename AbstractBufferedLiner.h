#ifndef READER_ABSTRACTLINER_H
#define READER_ABSTRACTLINER_H

#include <string>

class AbstractBufferedLiner {
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
    bool getline(std::string &line);
    explicit AbstractBufferedLiner(int bufLen);
    virtual ~AbstractBufferedLiner();
};

#endif //READER_ABSTRACTLINER_H
