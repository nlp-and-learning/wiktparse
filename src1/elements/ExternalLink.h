// elements/ExternalLink.h
#ifndef EXTERNAL_LINK_H
#define EXTERNAL_LINK_H

#include <string>
#include <optional>  // For optional display, C++17+
#include "WikiElement.h"  // Base class

namespace parser::elements {

    class ExternalLink : public WikiElement {
    public:
        // Constructor
        ExternalLink(const std::string& url, const std::optional<std::string>& display,
                     size_t start_pos, size_t end_pos);

        // Access methods
        const std::string& get_url() const { return url; }
        const std::optional<std::string>& get_display() const { return display; }

        // Override from WikiElement
        std::string to_string() const override;
        void accept(WikiVisitor& visitor) const override;

    private:
        std::string url;
        std::optional<std::string> display;  // Optional, if missing, use url as display
    };

}  // namespace parser::elements

#endif  // EXTERNAL_LINK_H