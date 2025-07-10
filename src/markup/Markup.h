#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <sstream>

struct Markup {
    virtual ~Markup() = default;
    [[nodiscard]] virtual std::string dump() const = 0;
};

class RichText: public Markup {
public:
    std::string text;
    explicit RichText(std::string text):text(std::move(text)){}
    [[nodiscard]] std::string dump() const override {
        return text;
    }
};

class Markups: public Markup {
public:
    std::vector<std::unique_ptr<Markup>> parts;
    [[nodiscard]] std::string dump() const override {
        std::ostringstream ss;
        for (auto &framgent: parts)
            ss << framgent->dump();
        return ss.str();
    }
};