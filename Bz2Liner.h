#ifndef READER_BZ2LINER_H
#define READER_BZ2LINER_H

#include <bzlib.h>
#include "AbstractLiner.h"

class Bz2Liner : public AbstractLiner {
    BZFILE *bzfile;
protected:
    void readBuffer() override;
public:
    Bz2Liner(int bufLen, BZFILE *bzfile) : AbstractLiner(bufLen), bzfile(bzfile) {}
};

#endif //READER_BZ2LINER_H
