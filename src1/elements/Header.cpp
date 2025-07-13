// elements/Header.cpp
#include "Header.h"
#include <string>
#include <algorithm>

namespace parser::elements {

    // Constructor
    Header::Header(int level, const std::string& title, size_t start_pos, size_t end_pos)
        : LeafElement(start_pos, end_pos),
          level(level),
          title(title) {
        // Optional validation: level 1-6
        if (level < 1 || level > 6) {
            throw std::invalid_argument("Header level must be between 1 and 6");
        }
    }

    // Override to_string (header reconstruction)
    std::string Header::to_string() const {
        return std::string(level, '=') + title + std::string(level, '=');
    }

    // Override accept (Visitor pattern)
    void Header::accept(WikiVisitor& visitor) const {
        visitor.visit(*this);
    }

}  // namespace parser::elements