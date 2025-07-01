#pragma once
#include <iostream>

class Progress {
    size_t filesize;
    int lastPercent = -1;
public:
    explicit Progress(size_t filesize) : filesize(filesize) {}
    void update(size_t currentPos);
};
