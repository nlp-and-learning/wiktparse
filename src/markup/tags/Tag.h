#pragma once
#include <string>
#include <utility>
#include <vector>

enum class TagType {
    Open,
    Close,
    SelfClosing,
    Invalid,
};

struct Tag {
    TagType type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    [[nodiscard]] std::string dump(uint32_t mask) const {
        std::ostringstream ss;
        ss << "<";
        ss << name;
        ss << ">";
        return ss.str();
    }
};
