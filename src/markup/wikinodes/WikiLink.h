#pragma once
#include "../Markup.h"

class WikiLink: public Markup {
public:
    std::vector<std::string> parts;
    std::string suffix;
    std::string target;
    std::string display;
    [[nodiscard]] std::string dump() const override {
        std::ostringstream ss;
        ss << "[[";
        for (int i=0; i < parts.size(); i++) {
            if (i>0)
                ss << "|";
            ss << parts[i];
        }
        ss << "]]";
        ss << suffix;
        return ss.str();
    }
};

class WikiLink1: public Markup {
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
