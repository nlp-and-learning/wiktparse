// elements/TextElement.cpp
#include "TextElement.h"
#include <sstream>

namespace parser::elements {

    // Constrcktor
    TextElement::TextElement(const std::string& text, bool active, size_t start_pos, size_t end_pos)
        : WikiElement(start_pos, end_pos),
          text(text),
          active(active) {}

    // Override to_string (text reconstruction)
    std::string TextElement::to_string() const {
        return text;  // Simple reconstruction - return raw text
    }

    // Override accept (Visitor pattern)
    void TextElement::accept(WikiVisitor& visitor) const {
        visitor.visit(*this);
    }

}  // namespace parser::elements