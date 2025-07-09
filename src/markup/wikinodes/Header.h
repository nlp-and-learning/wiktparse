#pragma once
#include <string>
#include "../Markup.h"

struct Header: public Markup {
    std::string name;
    size_t level;
};
