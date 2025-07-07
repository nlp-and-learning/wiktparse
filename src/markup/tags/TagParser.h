#pragma once
#include <cctype>
#include <sstream>
#include <cassert>
#include <memory>

#include "../BaseParser.h"
#include "../../tags/Tag.h"
#include "TagFactory.h"
#include "../Whitespace.h"

class TagParser: public BaseParser {
    TagFactory tagFactory = TagFactory::instance();
public:
    TagParser(const std::string& text, size_t pos) : BaseParser(text, pos) {}
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

    std::string parseAttrValue() {
        std::ostringstream ss_value;
        ++pos;
        Whitespace::skipWhitespace(text, pos);
        if (pos < text.size() && (text[pos] == '"' || text[pos] == '\'')) {
            char quote = text[pos++];

            while (pos < text.size() && text[pos] != quote) {
                ss_value << text[pos++];
            }
            if (pos < text.size() && text[pos] == quote)
                ++pos;
            return ss_value.str();
        } else {
            // unquoted attribute value
            while (pos < text.size() && !std::isspace(text[pos]) && text[pos] != '/' && text[pos] != '>') {
                ss_value << text[pos++];
            }
            return ss_value.str();
        }
    }

    std::string parseName() {
        std::ostringstream ss_attrName;
        while (pos < text.size() && isNameChar(text[pos]))
            ss_attrName << text[pos++];
        return ss_attrName.str();
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
        tag->name = parseName();
        if (tag->name.empty()) {
            tag->type = TagType::Invalid;
            assert(pos == start+1);
            return tag;
        }
        if (!tagFactory.has(tag->name)) {
            tag->type = TagType::Invalid;
            pos = start + 1;
            return tag;
        }

        // Parse attributes
        while (pos < text.size()) {
            Whitespace::skipWhitespace(text, pos);
            while (pos < text.size() && !isNameChar(text[pos])) {
                if (text[pos] == '/' && pos+1 < text.size() && text[pos + 1] == '>') {
                    if (tag->type == TagType::Open)
                        tag->type = TagType::SelfClosing;
                    pos += 2;
                    return tag;
                }
                if (text[pos] == '>') {
                    ++pos;
                    return tag;
                }
                if (text[pos] == '<') {
                    tag->type = TagType::Invalid;
                    pos = start + 1;
                    return tag;
                }
                ++pos;
            }
            std::string attrName = parseName();

            Whitespace::skipWhitespace(text, pos);

            std::string value;
            if (pos < text.size() && text[pos] == '=')
                value = parseAttrValue();
            tag->attributes.emplace_back(attrName, value);
        }

        // If we reach here, tag was not properly closed
        tag->type = TagType::Invalid;
        pos = start + 1;
        return tag;
    }
};
