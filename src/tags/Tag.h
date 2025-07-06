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


const uint32_t DUMP_DISPLAY = 1;
const uint32_t DUMP_COMMENT = 2;
const uint32_t DUMP_TAGS   =  4;
const uint32_t DUMP_ALL = DUMP_DISPLAY | DUMP_COMMENT | DUMP_TAGS;

enum class CommentStatus {
    Displayed,
    Commented,
};

struct TagBase {
    virtual ~TagBase() = default;
    [[nodiscard]] virtual std::string dump(uint32_t mask) const = 0;
};

struct TagFragment: TagBase {
    std::string text;
    CommentStatus commentStatus;
    TagFragment(std::string t, CommentStatus commentStatus)
        : text(std::move(t)), commentStatus(commentStatus) {}
    [[nodiscard]] std::string dump(uint32_t mask) const override {
        if (commentStatus == CommentStatus::Displayed) {
            if (mask  & DUMP_DISPLAY)
                return text;
            else
                return "";
        } else {
            if (mask  & DUMP_COMMENT)
                return text;
            else
                return "";
        }
    };
};

struct Tag: TagBase {
    TagType type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    [[nodiscard]] std::string dump(uint32_t mask) const override {
        if (!mask & DUMP_TAGS)
            return "";
        std::ostringstream ss;
        ss << "<";
        ss << name;
        ss << ">";
        return ss.str();
    }
};

struct TagStream: public TagBase {
    std::vector<std::unique_ptr<TagBase>> parts;
    [[nodiscard]] std::string dump(uint32_t mask) const override {
        std::ostringstream ss;
        for (auto &framgent: parts)
            ss << framgent->dump(mask);
        return ss.str();
    };
};
