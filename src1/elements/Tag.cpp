// elements/Tag.cpp
#include "Tag.h"
#include <sstream>  // For to_string, for tag reconstruction
#include <utility>  // For std::move, if needed

namespace parser::elements {

    // Constructor
    Tag::Tag(const std::string& name, const std::unordered_map<std::string, std::string>& attributes,
             TagType type, bool valid, size_t start_pos, size_t end_pos)
        : WikiElement(start_pos, end_pos),
          name(name),
          attributes(attributes),
          type(type),
          valid(valid) {}

    // Override to_string (tag reconstruction)
    std::string Tag::to_string() const {
        std::stringstream ss;
        ss << "<";
        if (is_closing()) {
            ss << "/";
        }
        ss << name;
        for (const auto& [key, val] : attributes) {
            ss << " " << key << "=\"" << val << "\"";
        }
        if (is_self_closing()) {
            ss << "/";
        }
        ss << ">";
        return ss.str();
    }

    // Override accept (Visitor pattern)
    void Tag::accept(WikiVisitor& visitor) const {
        visitor.visit(*this);
    }

}  // namespace parser::elements