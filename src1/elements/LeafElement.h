// elements/LeafElement.h
#ifndef LEAF_ELEMENT_H
#define LEAF_ELEMENT_H

#include "WikiElement.h"

namespace parser::elements {

    class LeafElement : public WikiElement {
    public:
        LeafElement(size_t start_pos, size_t end_pos) : WikiElement(start_pos, end_pos) {}
        // No add_child or content
    };

}  // namespace parser::elements

#endif  // LEAF_ELEMENT_H