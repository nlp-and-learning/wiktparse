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
};
