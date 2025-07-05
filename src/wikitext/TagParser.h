#pragma once
#include <cctype>
#include <sstream>
#include <cassert>
#include <memory>
#include "Tag.h"

class TagParser {
    static bool isWikiSpace(char c) {
        return c == ' ' || c == '\t';
    }
    static void skipWhitespace(const std::string &text, size_t &pos) {
        while (pos < text.size() && isWikiSpace(text[pos]))
            ++pos;
    }
public:
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
    static std::unique_ptr<Tag> parse(const std::string &text, size_t &pos) {
        assert(pos < text.size() && text[pos] == '<');
        size_t start = pos;
        ++pos; // skip '<'

        skipWhitespace(text, pos);

        auto tag = std::make_unique<Tag>();
        tag->type = TagType::Open;

        // Check for closing tag
        if (text[pos] == '/') {
            tag->type = TagType::Close;
            ++pos;
        }

        skipWhitespace(text, pos);

        // Parse tag name
        std::ostringstream name;
        while (pos < text.size() && (std::isalnum(text[pos]) || text[pos] == ':' || text[pos] == '-' || text[pos] == '_')) {
            name << text[pos++];
        }
        tag->name = name.str();
        if (tag->name.empty()) {
            tag->type = TagType::Invalid;
            pos = std::max(pos, start + 1); // move forward to avoid infinite loop
            return tag;
        }

        // Parse attributes
        while (pos < text.size()) {
            skipWhitespace(text, pos);
            if (text[pos] == '/' && text[pos + 1] == '>') {
                if (tag->type == TagType::Open)
                    tag->type = TagType::SelfClosing;
                pos += 2;
                return tag;
            } else if (text[pos] == '>') {
                ++pos;
                return tag;
            }

            // Parse attribute name
            std::ostringstream attrName;
            while (pos < text.size() && (std::isalnum(text[pos]) || text[pos] == ':' || text[pos] == '-' || text[pos] == '_')) {
                attrName << text[pos++];
            }
            skipWhitespace(text, pos);

            std::string value;
            if (pos < text.size() && text[pos] == '=') {
                ++pos;
                skipWhitespace(text, pos);
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
            tag->attributes.emplace_back(std::pmr::string(attrName.str()), std::pmr::string(value));
        }

        // If we reach here, tag was not properly closed
        tag->type = TagType::Invalid;
        pos = std::max(pos, start + 1); // move forward to avoid infinite loop
        return tag;
    }
};
