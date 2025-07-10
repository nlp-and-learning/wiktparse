#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct WikiText {
    virtual ~WikiText() = default;
    virtual std::string str() = 0;
};

class WikiFragment : public WikiText {
public:
    std::string text;
    bool isActive;
    WikiFragment(std::string text, bool isActive): text(std::move(text)), isActive(isActive) {}
    std::string str() override {
        return text;
    };
};

class WikiGroup : public WikiText {
public:
    std::vector<std::unique_ptr<WikiText>> parts;
    std::string str() override {
        std::ostringstream ss;
        for (auto &fragment: parts)
            ss << fragment->str();
        return ss.str();
    };
};