#pragma once
#include <cctype>
#include <sstream>
#include <cassert>
#include <memory>
#include "Tag.h"
#include "TagFactory.h"
#include "../util/Whitespace.h"

class TagParser {
    TagFactory tagFactory = TagFactory::instance();
    const std::string& text;
    size_t pos;
public:
    TagParser(const std::string& text, size_t pos) : text(text) , pos(pos) {}
    static bool startComment(const std::string &text, size_t &pos) {
        const std::string start = "<!--";
        if (text.starts_with(start)) {
            pos += start.size();
            return true;
        } else
            return false;
    }
    static bool endComment(const std::string &text, size_t &pos) {
        const std::string end = "-->";
        if (text.starts_with(end)) {
            pos += end.size();
            return true;
        } else
            return false;
    }
    bool isNameChar(unsigned char c) {
        return std::isalnum(text[pos]) || text[pos] == ':' || text[pos] == '-' || text[pos] == '_';
    }

    std::unique_ptr<Tag> parse() {
        if (pos >= text.size() || text[pos] != '<')
            throw std::logic_error("Unexpected end of input or not start with <");
        size_t start = pos;
        ++pos; // skip '<'

        Whitespace::skipWhitespace(text, pos);

        auto tag = std::make_unique<Tag>();
        tag->type = TagType::Open;

        // Check for closing tag
        if (text[pos] == '/') {
            tag->type = TagType::Close;
            ++pos;
        }

        Whitespace::skipWhitespace(text, pos);

        // Parse tag name
        std::ostringstream name;
        while (pos < text.size() && isNameChar(text[pos])) {
            name << text[pos++];
        }
        tag->name = name.str();
        if (tag->name.empty()) {
            tag->type = TagType::Invalid;
            assert(pos > start);
            return tag;
        }

        // Parse attributes
        while (pos < text.size()) {
            Whitespace::skipWhitespace(text, pos);
            if (text[pos] == '/' && text[pos + 1] == '>') {
                if (tag->type == TagType::Open)
                    tag->type = TagType::SelfClosing;
                pos += 2;
                if (!tagFactory.has(tag->name))
                    tag->type = TagType::Invalid;
                return tag;
            } else if (text[pos] == '>') {
                ++pos;
                if (!tagFactory.has(tag->name))
                    tag->type = TagType::Invalid;
                return tag;
            }
            while (pos < text.size() && !isNameChar(text[pos]))
                ++pos;
            // Parse attribute name
            std::ostringstream attrName;
            while (pos < text.size() && (std::isalnum(text[pos]) || text[pos] == ':' || text[pos] == '-' || text[pos] == '_')) {
                attrName << text[pos++];
            }
            Whitespace::skipWhitespace(text, pos);

            std::string value;
            if (pos < text.size() && text[pos] == '=') {
                ++pos;
                Whitespace::skipWhitespace(text, pos);
                if (pos < text.size() && (text[pos] == '"' || text[pos] == '\'')) {
                    char quote = text[pos++];
                    std::ostringstream val;
                    while (pos < text.size() && text[pos] != quote) {
                        val << text[pos++];
                    }
                    if (pos < text.size() && text[pos] == quote)
                        ++pos;
                    value = val.str();
                } else {
                    // unquoted attribute value
                    std::ostringstream val;
                    while (pos < text.size() && !std::isspace(text[pos]) && text[pos] != '/' && text[pos] != '>') {
                        val << text[pos++];
                    }
                    value = val.str();
                }
            }
            tag->attributes.emplace_back(std::string(attrName.str()), std::string(value));
        }

        // If we reach here, tag was not properly closed
        tag->type = TagType::Invalid;
        assert(pos > start);
        return tag;
    }
};
