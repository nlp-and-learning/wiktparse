// factory/TagFactory.cpp
#include "TagFactory.h"
#include "../handlers/TagHandler.h"  // For specific handlers, e.g. NowikiHandler

namespace parser::factory {

    // Singleton implementation
    TagFactory& TagFactory::get_instance() {
        static TagFactory instance;
        return instance;
    }

    bool TagFactory::has_handler(const std::string &name) const {
        return handlers.find(name) != handlers.end();
    }

    // Private constructor - handler registration
    TagFactory::TagFactory() {
        // Register handlers for known tags
        handlers["nowiki"] = std::make_unique<handlers::NowikiHandler>();
        handlers["sub"] = std::make_unique<handlers::SubHandler>();
        handlers["ref"] = std::make_unique<handlers::RefHandler>();
        handlers["br"] = std::make_unique<handlers::BrHandler>();
        handlers["span"] = std::make_unique<handlers::SpanHandler>();
        // ... for the remaining 50-60 tags, just examples to start with
    }

    // Method of creating a tag
    std::unique_ptr<elements::Tag> TagFactory::create_tag(const std::string& name,
                                                          const std::unordered_map<std::string, std::string>& attrs,
                                                          elements::TagType type, bool syntactically_valid) const {
        bool final_valid = syntactically_valid && (handlers.find(name) != handlers.end());

        // Create and return Tag (start/end positions are assumed to be provided by the parser later)
        return std::make_unique<elements::Tag>(name, attrs, type, final_valid, 0, 0);  // Tymczasowe pozycje
    }

}  // namespace parser::factory