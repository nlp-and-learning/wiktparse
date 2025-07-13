// elements/WikiLink.cpp
#include "WikiLink.h"
#include <sstream>
#include <utility>

namespace parser::elements {
    // Constructor
    WikiLink::WikiLink(const std::string& target, const std::optional<std::string>& display,
                       size_t start_pos, size_t end_pos)
        : LeafElement(start_pos, end_pos),
          target(target),
          display(display) {}

    // Override to_string (link reconstruction)
    std::string WikiLink::to_string() const {
        std::stringstream ss;
        ss << "[[" << target;
        if (display.has_value()) {
            ss << "|" << display.value();
        }
        ss << "]]";
        return ss.str();
    }

    // Override accept (Visitor pattern)
    void WikiLink::accept(WikiVisitor& visitor) const {
        visitor.visit(*this);
    }

}  // namespace parser::elements