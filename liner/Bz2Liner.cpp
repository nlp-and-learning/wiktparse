#include <iostream>
#include "Bz2Liner.h"

void Bz2Liner::readBuffer() {
    int bzerror;
    gcount = BZ2_bzRead(&bzerror, bzfile, buffer, bufLen);
    if (bzerror == BZ_STREAM_END)
        eof = true;
    if (bzerror != BZ_OK && bzerror != BZ_STREAM_END) {
        std::cerr << "Error reading from bz2 file\n";
        return;
    }
}
