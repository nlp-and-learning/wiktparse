// elements/TextElement.h
#ifndef TEXT_ELEMENT_H
#define TEXT_ELEMENT_H

#include "WikiElement.h"
#include <string>

namespace parser::elements {

    class TextElement : public WikiElement {
    public:
        TextElement(const std::string& text, bool active, size_t start_pos, size_t end_pos);

        const std::string& get_text() const { return text; }
        bool is_active() const { return active; }

        std::string to_string() const override;
        void accept(WikiVisitor& visitor) const override;

    private:
        std::string text;
        bool active;
    };

}  // namespace parser::elements

#endif  // TEXT_ELEMENT_H