// elements/ContainerElement.h
#ifndef CONTAINER_ELEMENT_H
#define CONTAINER_ELEMENT_H

#include "WikiElement.h"
#include <vector>
#include <memory>

namespace parser::elements {

    class ContainerElement : public WikiElement {
    public:
        ContainerElement(size_t start_pos, size_t end_pos) : WikiElement(start_pos, end_pos) {}
        void add_child(std::unique_ptr<WikiElement> child) { children.push_back(std::move(child)); }
        const std::vector<std::unique_ptr<WikiElement>>& get_children() const { return children; }
        std::vector<std::unique_ptr<WikiElement>> take_children() { return std::move(children); }

    protected:
        std::vector<std::unique_ptr<WikiElement>> children;
    };

}  // namespace parser::elements

#endif  // CONTAINER_ELEMENT_H