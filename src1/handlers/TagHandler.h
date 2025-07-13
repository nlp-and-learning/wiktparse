// src1/handlers/TagHandler.h
#ifndef TAG_HANDLER_H
#define TAG_HANDLER_H

#include <string>
#include <memory>  // Dla unique_ptr
#include "../elements/Tag.h"
#include "../elements/TaggedContent.h"

namespace parser::handlers {
    using parser::elements::Tag;
    using parser::elements::TaggedContent;

    class TagHandler {
    public:
        virtual ~TagHandler() = default;
        virtual void handle_open(const Tag& tag, TaggedContent& content) = 0;
        virtual void handle_close(const Tag& tag) = 0;
        // Other virtual methods...
    };

    // Specific handlers (empty at startup)
    class NowikiHandler : public TagHandler {
    public:
        void handle_open(const Tag& tag, TaggedContent& content) override {
            // Logic for <nowiki>, e.g. set active=false
        }
        void handle_close(const Tag& tag) override {
            // ...
        }
    };

    class SubHandler : public TagHandler {
    public:
        void handle_open(const Tag& tag, TaggedContent& content) override {}
        void handle_close(const Tag& tag) override {}
    };

    class RefHandler : public TagHandler {
    public:
        void handle_open(const Tag& tag, TaggedContent& content) override {}
        void handle_close(const Tag& tag) override {}
    };

    class BrHandler : public TagHandler {
    public:
        void handle_open(const Tag& tag, TaggedContent& content) override {}
        void handle_close(const Tag& tag) override {}
    };

    class SpanHandler : public TagHandler {
    public:
        void handle_open(const Tag& tag, TaggedContent& content) override {}
        void handle_close(const Tag& tag) override {}
    };

    // ... Add the rest of 50-60 as empty classes

}  // namespace parser::handlers

#endif