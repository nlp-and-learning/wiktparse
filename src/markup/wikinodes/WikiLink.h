#pragma once
#include "../Markup.h"

class WikiLink: public Markup {
    bool extended = false;
public:
    std::string target;
    std::string display;
    [[nodiscard]] std::string dump() const override {
        std::string out = "[[" + target;
        out += "|" + display;
        out += "]]";
        return out;
    }
};
