#pragma once
#include "../Markup.h"

class WikiLink: public Markup {
    bool extended = false;
public:
    std::string target;
    std::string display;
};
