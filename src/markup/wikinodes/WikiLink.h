#pragma once
#include "../Markup.h"

class WikiLink: public Markup {
public:
    std::vector<std::unique_ptr<Markup>> parts;
    std::string suffix;
    std::string target;
    [[nodiscard]] std::string dump() const override {
        std::ostringstream ss;
        ss << "[[";
        for (int i=0; i < parts.size(); i++) {
            if (i>0)
                ss << "|";
            ss << parts[i]->dump();
        }
        ss << "]]";
        ss << suffix;
        return ss.str();
    }
};
