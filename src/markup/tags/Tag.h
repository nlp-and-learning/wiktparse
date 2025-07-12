#pragma once
#include <string>
#include <utility>
#include <vector>
#include "../Markup.h"

enum class TagType {
    Open,
    Close,
    SelfClosing,
    Invalid,
};

struct Tag: public Markup {
    TagType type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    [[nodiscard]] std::string dump() const override {
        std::ostringstream ss;
        ss << "<";
        if (type == TagType::Close)
            ss << "/";
        ss << name;
        if (type == TagType::SelfClosing)
            ss << " /";
        ss << ">";
        return ss.str();
    }
    [[nodiscard]] std::string rawText() const override {
        return "";
    }
    [[nodiscard]] bool matchesClose(const Tag& closeTag) const {
        return type == TagType::Open && closeTag.type == TagType::Close && name == closeTag.name;
    }
};
