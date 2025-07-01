#include "Progress.h"

void Progress::update(size_t currentPos) {
    if (filesize == 0) return; // unikamy dzielenia przez 0
    int percent = static_cast<int>((100.0 * currentPos) / filesize);
    if (percent > lastPercent) {
        lastPercent = percent;
        std::cout << percent << "%\n";
    }
}

