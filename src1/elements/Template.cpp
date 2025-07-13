// elements/Template.cpp
#include "Template.h"
#include <sstream>
#include <utility>

namespace parser::elements {

    // Constructor
    Template::Template(const std::string& name, const std::unordered_map<std::string, std::string>& params,
                       size_t start_pos, size_t end_pos)
        : LeafElement(start_pos, end_pos),
          name(name),
          params(params) {}

    // Override to_string (template reconstruction)
    std::string Template::to_string() const {
        std::stringstream ss;
        ss << "{{" << name;
        for (const auto& [key, val] : params) {
            if (key == std::to_string(1)) {  // For numbered parameters, without a key
                ss << "|" << val;
            } else {
                ss << "|" << key << "=" << val;
            }
        }
        ss << "}}";
        return ss.str();
    }

    // Override accept (Visitor pattern)
    void Template::accept(WikiVisitor& visitor) const {
        visitor.visit(*this);
    }

}  // namespace parser::elements