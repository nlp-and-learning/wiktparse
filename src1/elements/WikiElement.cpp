// elements/WikiElement.cpp
#include "WikiElement.h"

namespace parser::elements {

    // Constructor with validation
    WikiElement::WikiElement(size_t start_pos, size_t end_pos)
        : start_pos(start_pos), end_pos(end_pos) {
        if (end_pos < start_pos) {
            throw std::invalid_argument("End pos < start pos");
        }
    }

}  // namespace parser::elements