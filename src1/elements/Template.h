// elements/Template.h
#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <string>
#include <unordered_map>
#include "WikiElement.h"  // Base class

namespace parser::elements {

    class Template : public WikiElement {
    public:
        // Constructor
        Template(const std::string& name, const std::unordered_map<std::string, std::string>& params,
                 size_t start_pos, size_t end_pos);

        // Access methods
        const std::string& get_name() const { return name; }
        const std::unordered_map<std::string, std::string>& get_params() const { return params; }

        // Override from WikiElement
        std::string to_string() const override;
        void accept(WikiVisitor& visitor) const override;

    private:
        std::string name;
        std::unordered_map<std::string, std::string> params;
    };

}  // namespace parser::elements

#endif  // TEMPLATE_H