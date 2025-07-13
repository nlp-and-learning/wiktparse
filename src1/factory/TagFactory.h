// factory/TagFactory.h
#ifndef TAG_FACTORY_H
#define TAG_FACTORY_H

#include <string>
#include <unordered_map>
#include <memory>  // For unique_ptr
#include "../handlers/TagHandler.h"
#include "../elements/Tag.h"

namespace parser::factory {

    class TagFactory {
    public:
        // Singleton access
        static TagFactory& get_instance();

        // Method to creating a tag
        std::unique_ptr<elements::Tag> create_tag(const std::string& name,
                                                  const std::unordered_map<std::string, std::string>& attrs,
                                                  elements::TagType type) const;

        // No copying or assignment
        TagFactory(const TagFactory&) = delete;
        TagFactory& operator=(const TagFactory&) = delete;

    private:
        // Private constructor (handler registration)
        TagFactory();

        // Map of handlers
        std::unordered_map<std::string, std::unique_ptr<handlers::TagHandler>> handlers;
    };

}  // namespace parser::factory

#endif  // TAG_FACTORY_H