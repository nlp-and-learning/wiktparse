// elements/ExternalLink.cpp
#include "ExternalLink.h"
#include <sstream>  // For to_string reconstruction

namespace parser::elements {

    // Constructor
    ExternalLink::ExternalLink(const std::string& url, const std::optional<std::string>& display,
                               size_t start_pos, size_t end_pos)
        : LeafElement(start_pos, end_pos),
          url(url),
          display(display) {}

    // Override to_string (reconstruct external link)
    std::string ExternalLink::to_string() const {
        std::stringstream ss;
        ss << "[" << url;
        if (display.has_value()) {
            ss << " " << display.value();
        }
        ss << "]";
        return ss.str();
    }

    // Override accept (Visitor pattern)
    void ExternalLink::accept(WikiVisitor& visitor) const {
        visitor.visit(*this);
    }

}  // namespace parser::elements