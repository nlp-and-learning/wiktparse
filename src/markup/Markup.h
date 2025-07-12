#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <sstream>

#include "Whitespace.h"

struct Markup {
    virtual ~Markup() = default;
    [[nodiscard]] virtual std::string dump() const = 0;
    [[nodiscard]] virtual std::string displayText() const = 0;
};

class RichText: public Markup {
public:
    std::string text;
    explicit RichText(std::string text):text(std::move(text)){}
    [[nodiscard]] std::string dump() const override {
        return text;
    }
    [[nodiscard]] std::string displayText() const override {
        return Whitespace::compact(text);
    };
};

class Markups: public Markup {
public:
    std::vector<std::unique_ptr<Markup>> parts;
    [[nodiscard]] std::string dump() const override {
        std::ostringstream ss;
        for (auto &fragment: parts)
            ss << fragment->dump();
        return ss.str();
    }
    [[nodiscard]] std::string displayText() const override {
        std::ostringstream ss;
        for (auto &fragment: parts)
            ss << fragment->displayText();
        return ss.str();
    };
};