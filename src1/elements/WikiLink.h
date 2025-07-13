// elements/WikiLink.h
#ifndef WIKI_LINK_H
#define WIKI_LINK_H

#include <string>
#include <optional>
#include "WikiElement.h"  // Base class

namespace parser::elements {

    class WikiLink : public WikiElement {
    public:
        // Constructor
        WikiLink(const std::string& target, const std::optional<std::string>& display,
                 size_t start_pos, size_t end_pos);

        // Access methods
        const std::string& get_target() const { return target; }
        const std::optional<std::string>& get_display() const { return display; }

        // Override from WikiElement
        std::string to_string() const override;
        void accept(WikiVisitor& visitor) const override;

    private:
        std::string target;
        std::optional<std::string> display;  // Optional, if missing, use target as display
    };

}  // namespace parser::elements

#endif  // WIKI_LINK_H