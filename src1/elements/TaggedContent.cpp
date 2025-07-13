// elements/TaggedContent.cpp
#include "TaggedContent.h"
#include <sstream>  // For to_string, if text reconstruction

namespace parser::elements {

    // Constructor
    TaggedContent::TaggedContent(std::unique_ptr<Tag> opening_tag, size_t start_pos, size_t end_pos)
        : WikiElement(start_pos, end_pos),
          opening_tag(std::move(opening_tag)),
          content(),
          closing_tag(nullptr) {}

    // Adding a child
    void TaggedContent::add_child(std::unique_ptr<WikiElement> element) {
        if (element) {
            content.push_back(std::move(element));
        }
    }

    // Validation (check tag matching)
    bool TaggedContent::validate() const {
        if (!closing_tag) {
            return false;  // Missing closing tag
        }
        if (!opening_tag || opening_tag->get_name() != closing_tag->get_name()) {
            return false;  // Incompatible names
        }
        if (opening_tag->get_type() != TagType::OPENING || closing_tag->get_type() != TagType::CLOSING) {
            return false;  // Incorrect types
        }
        return true;
    }

    // Set closing tag
    void TaggedContent::set_closing_tag(std::unique_ptr<Tag> closing) {
        closing_tag = std::move(closing);
    }

    // Override to_string (text reconstruction)
    std::string TaggedContent::to_string() const {
        std::stringstream ss;
        if (opening_tag) {
            ss << opening_tag->to_string();
        }
        for (const auto& child : content) {
            ss << child->to_string();
        }
        if (closing_tag) {
            ss << closing_tag->to_string();
        }
        return ss.str();
    }

    // Override accept (Visitor pattern)
    void TaggedContent::accept(WikiVisitor& visitor) const {
        visitor.visit(*this);
    }

}  // namespace parser::elements