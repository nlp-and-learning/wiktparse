// elements/TaggedContent.h
#ifndef TAGGED_CONTENT_H
#define TAGGED_CONTENT_H

#include <vector>
#include <memory>
#include <optional>
#include "WikiElement.h"
#include "Tag.h"  // Dla Tag

namespace parser::elements {

    class TaggedContent : public WikiElement {
    public:
        // Constructor
        TaggedContent(std::unique_ptr<Tag> opening_tag, size_t start_pos, size_t end_pos);

        // Access methods
        const Tag* get_opening_tag() const { return opening_tag.get(); }
        const Tag* get_closing_tag() const { return closing_tag ? closing_tag.get() : nullptr; }
        std::vector<std::unique_ptr<WikiElement>> take_content() { return std::move(content); }
        const std::vector<std::unique_ptr<WikiElement>>& get_content() const { return content; }

        // Adding a child
        void add_child(std::unique_ptr<WikiElement> element);

        // Validation (check tag matching)
        bool validate() const;

        // Set closing tag
        void set_closing_tag(std::unique_ptr<Tag> closing);

        // Override from WikiElement
        std::string to_string() const override;
        void accept(WikiVisitor& visitor) const override;

    private:
        std::unique_ptr<Tag> opening_tag;
        std::vector<std::unique_ptr<WikiElement>> content;
        std::unique_ptr<Tag> closing_tag;  // Optionally nullptr if unmatched
    };

}  // namespace parser::elements

#endif  // TAGGED_CONTENT_H