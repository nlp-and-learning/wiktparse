// elements/Header.h
#ifndef HEADER_H
#define HEADER_H

#include <string>
#include "WikiElement.h"  // Bazowa klasa

namespace parser::elements {

    class Header : public WikiElement {
    public:
        // Constructor
        Header(int level, const std::string& title, size_t start_pos, size_t end_pos);

        // Access methods
        int get_level() const { return level; }
        const std::string& get_title() const { return title; }

        // Override from WikiElement
        std::string to_string() const override;
        void accept(WikiVisitor& visitor) const override;

    private:
        int level;
        std::string title;
    };

}  // namespace parser::elements

#endif  // HEADER_H