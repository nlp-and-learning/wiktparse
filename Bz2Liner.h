#ifndef READER_BZ2LINER_H
#define READER_BZ2LINER_H

#include <bzlib.h>
#include "AbstractBufferedLiner.h"

class Bz2Liner : public AbstractBufferedLiner {
    BZFILE *bzfile;
protected:
    void readBuffer() override;
public:
    Bz2Liner(int bufLen, BZFILE *bzfile) : AbstractBufferedLiner(bufLen), bzfile(bzfile) {}
};

#endif //READER_BZ2LINER_H
