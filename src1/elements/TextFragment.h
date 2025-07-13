// elements/TextFragment.h
#ifndef TEXT_FRAGMENT_H
#define TEXT_FRAGMENT_H

#include <string>
#include "WikiElement.h"  // Base class

namespace parser::elements {

    class TextFragment : public WikiElement {
    public:
        // Constructor
        TextFragment(const std::string& text, bool active, size_t start_pos, size_t end_pos);

        // Access methods
        const std::string& get_text() const { return text; }
        bool is_active() const { return active; }

        // Override from WikiElement
        std::string to_string() const override;
        void accept(WikiVisitor& visitor) const override;

    private:
        std::string text;
        bool active;
    };

}  // namespace parser::elements

#endif  // TEXT_FRAGMENT_H