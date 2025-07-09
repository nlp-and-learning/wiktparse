#pragma once
#include <memory>
#include <string>
#include <vector>

struct Markup {
    virtual ~Markup() = default;
};

class RichText : public Markup {
public:
    std::string text;
};

class Markups : public Markup {
public:
    std::vector<std::unique_ptr<Markup>> parts;
};