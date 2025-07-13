// factory/TagFactory.cpp
#include "TagFactory.h"
#include "../handlers/TagHandler.h"  // For specific handlers, e.g. NowikiHandler

namespace parser::factory {

    // Singleton implementation
    TagFactory& TagFactory::get_instance() {
        static TagFactory instance;
        return instance;
    }

    // Private constructor - handler registration
    TagFactory::TagFactory() {
        // Register handlers for known tags
        handlers["nowiki"] = std::make_unique<handlers::NowikiHandler>();
        // Add more, e.g.:
        // handlers["ref"] = std::make_unique<handlers::RefHandler>();
        // handlers["br"] = std::make_unique<handlers::BrHandler>();
        // ... for the remaining 50-60 tags, just examples to start with
    }

    // Method of creating a tag
    std::unique_ptr<elements::Tag> TagFactory::create_tag(const std::string& name,
                                                          const std::unordered_map<std::string, std::string>& attrs,
                                                          elements::TagType type) const {
        bool valid = true;  // Basic validation, e.g., check name
        // If the handler exists, use specific logic (e.g., attribute validation).
        auto it = handlers.find(name);
        if (it != handlers.end()) {
            // For example: the handler can modify valid or attrs, but start simple
            // it->second->validate_attrs(attrs);  // If you add such a method in TagHandler
        } else {
            // For unknown tags, mark as invalid if necessary
            // valid = false;
        }

        // Create and return Tag (start/end positions are assumed to be provided by the parser later)
        return std::make_unique<elements::Tag>(name, attrs, type, valid, 0, 0);  // Tymczasowe pozycje
    }

}  // namespace parser::factory