#pragma once
#include <optional>
#include "../Markup.h"

struct TemplateParameter {
    std::optional<std::string> name; // nullopt = pozycyjny
    std::unique_ptr<Markup> value;
    TemplateParameter(std::optional<std::string> name, std::unique_ptr<Markup> value)
        : name(std::move(name)), value(std::move(value)) {}
    [[nodiscard]] std::string dump() const {
        std::string s = "|";
        if (name) {
            s += *name + "=";
        }
        s += value->dump();
        return s;
    }
};

class Template: public Markup {
public:
    std::string name;
    std::vector<TemplateParameter> parameters;
    [[nodiscard]] std::string dump() const override {
        std::string out = "{{" + name;
        for (const auto& p : parameters)
            out += p.dump();
        out += "}}";
        return out;
    }
};
