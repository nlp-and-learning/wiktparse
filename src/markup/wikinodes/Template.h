#pragma once
#include <optional>
#include "../Markup.h"

struct TemplateParameter {
    std::optional<std::string> name; // nullopt = pozycyjny
    std::unique_ptr<Markup> value;
    TemplateParameter(std::optional<std::string> name, std::unique_ptr<Markup> value)
        : name(std::move(name)), value(std::move(value)) {}
    static std::string indent(int n) {
        if (n<=0) return "";
        std::string indentStr(n, ' ');
        return indentStr;
    }
    [[nodiscard]] std::string dump() const {
        std::string s = "|";
        if (name) {
            s += *name + "=";
        }
        s += value->dump();
        return s;
    }

    [[nodiscard]] size_t nameLen() const{
        if (name)
            return (*name).size();
        return 0;
    }

    [[nodiscard]] std::string printAlign(size_t nameField) const {
        std::string s = "|";
        if (name) {
            s += ' ' + *name + indent((int)nameField - int((*name).size())) + "= ";
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

    [[nodiscard]] std::string formatStr() const {
        std::string out = "{{" + name;
        size_t maxNameLen = 0;
        for (const auto& p : parameters)
            maxNameLen = std::max(maxNameLen, p.nameLen());
        for (const auto& p : parameters)
            out += "\n" + p.printAlign(maxNameLen + 1);
        out += "\n}}";
        return out;
    }
};
