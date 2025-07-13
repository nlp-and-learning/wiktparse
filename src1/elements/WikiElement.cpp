// elements/WikiElement.cpp
#include "WikiElement.h"

namespace parser::elements {

    // Constructor without validation for end_pos=0 (allow temporary unset)
    WikiElement::WikiElement(size_t start_pos, size_t end_pos)
        : start_pos(start_pos), end_pos(end_pos) {
        if (end_pos != 0 && end_pos < start_pos) {
            throw std::invalid_argument("End pos < start pos");
        }
    }

}  // namespace parser::elements