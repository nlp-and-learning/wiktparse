#pragma once
#include <memory>
#include <string>
#include <vector>

struct WikiText {
    virtual ~WikiText() = default;
};

class WikiFragment : public WikiText {
public:
    std::string text;
    bool isPlain;
};

class WikiGroup : public WikiText {
public:
    std::vector<std::unique_ptr<WikiText>> parts;
};