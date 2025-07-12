#pragma once
#include <string>
#include "../Markup.h"

struct Header: public Markup {
    std::string name;
    size_t level;
    [[nodiscard]] std::string dump() const override {
        std::string eqs(level, '=');
        return eqs + name + eqs;
    }
    [[nodiscard]] std::string rawText() const override {
        return name;
    }
};
