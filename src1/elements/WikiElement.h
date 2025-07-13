// elements/WikiElement.h
#ifndef WIKI_ELEMENT_H
#define WIKI_ELEMENT_H

#include <string>
#include <stdexcept>
#include <cstddef>  // For size_t
#include "WikiVisitor.h"

namespace parser::elements {

    class WikiElement {
    public:
        // Virtual destructor
        virtual ~WikiElement() = default;

        // Constructor
        WikiElement(size_t start_pos, size_t end_pos);

        // Access methods
        size_t get_start_pos() const { return start_pos; }
        size_t get_end_pos() const { return end_pos; }

        // Setters with validation
        void set_start_pos(size_t pos) {
            if (pos > end_pos) throw std::invalid_argument("Start pos > end pos");
            start_pos = pos;
        }
        void set_end_pos(size_t pos) {
            if (pos < start_pos) throw std::invalid_argument("End pos < start pos");
            end_pos = pos;
        }

        // Abstract methods
        virtual std::string to_string() const = 0;
        virtual void accept(WikiVisitor& visitor) const = 0;



    protected:
        size_t start_pos;
        size_t end_pos;
    };

}  // namespace parser::elements

#endif  // WIKI_ELEMENT_H