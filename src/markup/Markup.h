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
    [[nodiscard]] virtual std::string rawText() const = 0;
    [[nodiscard]] std::string displayText() const {
        return Whitespace::compact(rawText());
    };
};

class RichText: public Markup {
public:
    std::string text;
    explicit RichText(std::string text):text(std::move(text)){}
    [[nodiscard]] std::string dump() const override {
        return text;
    }
    [[nodiscard]] std::string rawText() const override {
        return text;
    }
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
    [[nodiscard]] std::string rawText() const override {
        std::ostringstream ss;
        for (auto &fragment: parts)
            ss << fragment->rawText();
        return ss.str();
    };
};