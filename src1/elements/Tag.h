// elements/Tag.h
#ifndef TAG_H
#define TAG_H

#include <string>
#include <unordered_map>

#include "LeafElement.h"
#include "WikiElement.h"  // Base class

namespace parser::elements {

    // Enum for tag types
    enum class TagType {
        OPENING,
        CLOSING,
        SELF_CLOSING,
        INVALID
    };

    class Tag : public LeafElement {
    public:
        // Constructor
        Tag(const std::string& name, const std::unordered_map<std::string, std::string>& attributes,
            TagType type, bool valid, size_t start_pos, size_t end_pos);

        // Access methods
        const std::string& get_name() const { return name; }
        const std::unordered_map<std::string, std::string>& get_attributes() const { return attributes; }
        TagType get_type() const { return type; }
        bool is_valid() const { return valid; }

        // Auxiliary methods
        bool is_opening() const { return type == TagType::OPENING; }
        bool is_closing() const { return type == TagType::CLOSING; }
        bool is_self_closing() const { return type == TagType::SELF_CLOSING; }
        bool is_invalid() const { return type == TagType::INVALID; }

        // Override from WikiElement
        std::string to_string() const override;
        void accept(WikiVisitor& visitor) const override;

    private:
        std::string name;
        std::unordered_map<std::string, std::string> attributes;
        TagType type;
        bool valid;
    };

}  // namespace parser::elements

#endif  // TAG_H