// parser/Parser.cpp
#include "Parser.h"
#include "../elements/Template.h"  // Dla Template
#include "../elements/Header.h"
#include "../elements/WikiLink.h"
#include "../elements/ExternalLink.h"
#include "../elements/Tag.h"
#include "../elements/TextElement.h"
#include "../elements/TaggedContent.h"
#include "../factory/TagFactory.h"  // Dla TagFactory
#include <regex>  // Dla regex
#include <algorithm>  // Dla std::find, etc.
#include <stack>    // Dla stack TaggedContent

namespace parser {

Parser::Parser(const std::string& input_text) : input(input_text), pos(0) {}

// Main parse with hierarchy for tags
std::vector<std::unique_ptr<elements::WikiElement>> Parser::parse() {
    std::stack<elements::TaggedContent*> container_stack;
    auto root = std::make_unique<elements::TaggedContent>(nullptr, 0, input.size());  // Dummy root container
    container_stack.push(root.get());

    while (pos < input.size()) {
        size_t start = pos;
        StarterType type = detect_starter(start);
        std::unique_ptr<elements::WikiElement> elem;
        switch (type) {
            case StarterType::TAG: {
                auto old_pos = pos;
                auto tag = parse_tag(input, pos);
                if (tag) {
                    if (tag->is_opening()) {
                        auto content = std::make_unique<elements::TaggedContent>(std::move(tag), start, 0);  // End pos set later
                        container_stack.top()->add_child(std::move(content));
                        container_stack.push(static_cast<elements::TaggedContent*>(container_stack.top()->get_content().back().get()));  // Push new container
                    } else if (tag->is_closing()) {
                        if (container_stack.size() > 1 && container_stack.top()->get_opening_tag() &&
                            container_stack.top()->get_opening_tag()->get_name() == tag->get_name()) {
                            container_stack.top()->set_closing_tag(std::move(tag));
                            container_stack.top()->set_end_pos(pos);
                            container_stack.pop();
                        } else {
                            // Unmatched closing, treat as invalid or text
                            pos = start;
                            elem = parse_text(input, pos);
                            container_stack.top()->add_child(std::move(elem));
                        }
                    } else {  // Self-closing or invalid
                        elem = std::move(tag);
                        container_stack.top()->add_child(std::move(elem));
                    }
                } else {
                    // Invalid tag, parse as text
                    pos = old_pos;;
                    elem = parse_text(input, pos);
                    container_stack.top()->add_child(std::move(elem));
                }
                break;
            }
            case StarterType::TEMPLATE:
                elem = parse_template(input, pos);
                container_stack.top()->add_child(std::move(elem));
                break;
            case StarterType::WIKILINK:
                elem = parse_wikilink(input, pos);
                container_stack.top()->add_child(std::move(elem));
                break;
            case StarterType::EXTERNAL_LINK:
                elem = parse_external_link(input, pos);
                container_stack.top()->add_child(std::move(elem));
                break;
            case StarterType::HEADER:
                elem = parse_header(input, pos);
                container_stack.top()->add_child(std::move(elem));
                break;
            default:
                elem = parse_text(input, pos);
                container_stack.top()->add_child(std::move(elem));
                break;
        }
    }

    // Handle unmatched open tags if any (optional: throw or close)
    while (container_stack.size() > 1) {
        container_stack.top()->set_end_pos(input.size());
        container_stack.pop();
    }

    return root->take_content();
}

// Detect starter
StarterType Parser::detect_starter(size_t& start_pos) {
    if (pos + 1 < input.size()) {
        if (input.substr(pos, 2) == "{{") return StarterType::TEMPLATE;
        if (input.substr(pos, 2) == "[[") return StarterType::WIKILINK;
        if (input[pos] == '[') return StarterType::EXTERNAL_LINK;
        if (input[pos] == '<') return StarterType::TAG;
        if (input[pos] == '=' && (pos == 0 || input[pos-1] == '\n')) return StarterType::HEADER;  // Beginning of line
    }
    return StarterType::NONE;
}

// Example subparser for tag (parse from pos, advance pos)
std::unique_ptr<elements::Tag> Parser::parse_tag(const std::string& input, size_t& pos) {
    size_t start = pos;
    if (pos >= input.size() || input[pos] != '<') {
        return nullptr;  // Not a tag starter
    }
    pos++;  // Skip <

    // Parse the tag content until >
    size_t content_start = pos;
    while (pos < input.size() && input[pos] != '>') {
        ++pos;
    }
    if (pos >= input.size() || input[pos] != '>') {
        pos = start + 1;  // Reset to after <, treat as text
        return nullptr;
    }
    std::string tag_content = input.substr(content_start, pos - content_start);
    pos++;  // Skip >

    // Use regex to extract closing, name, attrs, self-closing
    std::regex tag_pattern(R"((/?)(\w+)([^/]*)(/?))");
    std::smatch m;
    if (!std::regex_match(tag_content, m, tag_pattern)) {
        pos = start + 1;  // Invalid, reset
        return nullptr;
    }

    bool is_closing = !m[1].str().empty();
    std::string name = m[2].str();
    std::string attrs_str = m[3].str();
    bool is_self_closing = !m[4].str().empty();

    // Determine type
    elements::TagType type = elements::TagType::INVALID;
    if (is_closing && is_self_closing) {
        // Invalid combo
    } else if (is_closing) {
        type = elements::TagType::CLOSING;
    } else if (is_self_closing) {
        type = elements::TagType::SELF_CLOSING;
    } else {
        type = elements::TagType::OPENING;
    }

    // Parse attributes
    std::unordered_map<std::string, std::string> attributes;
    std::string attr_pattern_str = "\\s*(\\w+)\\s*=\\s*(\"([^\"]*)\"|'([^']*)'|([^ \\t>]+))";
    std::regex attr_pattern(attr_pattern_str);
    std::sregex_iterator iter(attrs_str.begin(), attrs_str.end(), attr_pattern);
    std::sregex_iterator end;
    for (; iter != end; ++iter) {
        std::string key = (*iter)[1].str();
        std::string val;
        if ((*iter)[3].matched) {  // Double quoted
            val = (*iter)[3].str();
        } else if ((*iter)[4].matched) {  // Single quoted
            val = (*iter)[4].str();
        } else {  // Unquoted
            val = (*iter)[5].str();
        }
        attributes[key] = val;
    }

    // Syntax valid if type not INVALID
    bool syntactically_valid = (type != elements::TagType::INVALID);

    // Create via factory (final validation)
    auto tag = factory::TagFactory::get_instance().create_tag(name, attributes, type, syntactically_valid, start, pos);
    if (tag == nullptr || !tag->is_valid()) {
        pos = start + 1;  // Invalid, treat as text "<"
        return nullptr;
    }

    return tag;
}

// For template (with balancing)
std::unique_ptr<elements::Template> Parser::parse_template(const std::string& input, size_t& pos) {
    size_t start = pos;
    pos += 2;  // Skip {{
    std::string content;
    if (balance_delimiters("{{", "}}", input, pos, content)) {
        // Parse content to name/params
        std::vector<std::string> parts;
        size_t p = 0;
        int depth = 0;
        std::string current_part;
        while (p < content.size()) {
            if (content[p] == '{' || content[p] == '[') depth++;
            else if (content[p] == '}' || content[p] == ']') depth--;
            else if (content[p] == '|' && depth == 0) {
                parts.push_back(current_part);
                current_part.clear();
                p++;
                continue;
            }
            current_part += content[p];
            p++;
        }
        if (!current_part.empty()) parts.push_back(current_part);

        if (parts.empty()) {
            pos = start;  // Reset on empty
            return nullptr;
        }

        std::string name = parts[0];
        std::unordered_map<std::string, std::string> params;
        for (size_t i = 1; i < parts.size(); ++i) {
            size_t eq_pos = parts[i].find('=');
            if (eq_pos != std::string::npos) {
                std::string key = parts[i].substr(0, eq_pos);
                std::string val = parts[i].substr(eq_pos + 1);
                params[key] = val;
            } else {
                params[std::to_string(i)] = parts[i];
            }
        }

        return std::make_unique<elements::Template>(name, params, start, pos);
    } else {
        // Unbalanced, treat as text
        pos = start;
        return nullptr;
    }
}

std::unique_ptr<elements::WikiLink> Parser::parse_wikilink(const std::string &input, size_t &pos) {
    size_t start = pos;
    if (pos + 1 >= input.size() || input.substr(pos, 2) != "[[") {
        return nullptr;  // Not a wikilink starter
    }
    pos += 2;

    // Find end of inner: until "]]", no nesting (wikilinks don't nest)
    size_t inner_start = pos;
    size_t close_pos = input.find("]]", pos);
    if (close_pos == std::string::npos) {
        pos = start;  // Reset pos on failure
        return nullptr;  // Unclosed
    }
    std::string inner = input.substr(inner_start, close_pos - inner_start);
    pos = close_pos + 2;  // Advance past "]]"

    // Parse inner: target | display
    std::string target;
    std::optional<std::string> display;
    size_t pipe_pos = inner.find('|');
    if (pipe_pos != std::string::npos) {
        target = inner.substr(0, pipe_pos);
        display = inner.substr(pipe_pos + 1);
    } else {
        target = inner;
    }

    // Parse suffix after "]]": consecutive ASCII letters (isalpha)
    std::string suffix;
    while (pos < input.size() && std::isalpha(static_cast<unsigned char>(input[pos])) && static_cast<unsigned char>(input[pos]) <= 127) {
        suffix += input[pos];
        ++pos;
    }

    // Append suffix to display (or target if no display)
    if (!suffix.empty()) {
        if (display.has_value()) {
            display = display.value() + suffix;
        } else {
            display = target + suffix;
        }
    }

    // Create WikiLink with positions
    size_t end_pos = pos;
    return std::make_unique<elements::WikiLink>(target, display, start, end_pos);
}

 std::unique_ptr<elements::ExternalLink> Parser::parse_external_link(const std::string &input, size_t &pos) {
    size_t start = pos;
    if (pos >= input.size() || input[pos] != '[') {
        return nullptr;  // Not an external link starter
    }
    pos += 1;

    // Find end of inner: until "]", no nesting
    size_t inner_start = pos;
    size_t close_pos = input.find(']', pos);
    if (close_pos == std::string::npos) {
        pos = start;  // Reset pos on failure
        return nullptr;  // Unclosed
    }
    std::string inner = input.substr(inner_start, close_pos - inner_start);
    pos = close_pos + 1;  // Advance past "]"

    // Split inner: url [space description]
    size_t space_pos = inner.find(' ');
    std::string url;
    std::optional<std::string> display;
    if (space_pos != std::string::npos) {
        url = inner.substr(0, space_pos);
        std::string desc = inner.substr(space_pos + 1);

        // Trim leading/trailing spaces from description
        size_t desc_start = desc.find_first_not_of(" \t");
        if (desc_start != std::string::npos) {
            size_t desc_end = desc.find_last_not_of(" \t");
            display = desc.substr(desc_start, desc_end - desc_start + 1);
        }
    } else {
        url = inner;
    }

    // URL validation: scheme (ASCII letters, optional digits) followed by "://"
    std::regex url_scheme_regex(R"([a-zA-Z][a-zA-Z0-9]*://)");
    std::smatch match;
    if (!std::regex_search(url, match, url_scheme_regex) || match.position() != 0) {
        pos = start;  // Reset pos on invalid URL
        return nullptr;
    }

    // Create ExternalLink with positions
    size_t end_pos = pos;
    return std::make_unique<elements::ExternalLink>(url, display, start, end_pos);
}

 bool Parser::isWikiSpace(char c) {
     return std::strchr(" \t\f\v", static_cast<unsigned char>(c)) != nullptr;
 }

 size_t Parser::trimmedEolPos(const std::string &input, size_t pos) {
    auto posEol = input.find_first_of("\r\n", pos);
    if (posEol == std::string::npos)
        posEol = input.size();
    while (posEol>pos && isWikiSpace(input[posEol-1]))
        --posEol;
    return posEol;
 }

 std::unique_ptr<elements::Header> Parser::parse_header(const std::string &input, size_t &pos) {
    size_t start = pos;

    // Check if at start of line: previous char is \n or pos == 0
    if (pos != 0 && input[pos - 1] != '\n') {
        return nullptr;  // Not at line start
    }

    // Count leading '='
    size_t leading = 0;
    auto auxPos = pos;
    while (auxPos < input.size() && input[auxPos] == '=') {
        ++leading;
        ++auxPos;
    }

    if (leading == 0) {
        return nullptr;
    }

    auto end_pos = trimmedEolPos(input, pos);
    auxPos = end_pos - 1;
    while (auxPos >= pos && input[auxPos] == '=')
        auxPos--;
    auto trailing  = end_pos - 1 - auxPos;

    auto level = std::min(leading, trailing);
    if (level < 1 || level > 6) {
        return nullptr;
    }

    std::string title = input.substr(pos + level, (end_pos - pos) - 2 * level);
    pos = end_pos;

    // Trim whitespace from title
    size_t trim_start = title.find_first_not_of(" \t");
    if (trim_start == std::string::npos) {
        title.clear();
    } else {
        size_t trim_end = title.find_last_not_of(" \t");
        title = title.substr(trim_start, trim_end - trim_start + 1);
    }

    // Create Header
    return std::make_unique<elements::Header>(level, title, start, end_pos);
}

    std::unique_ptr<elements::TextElement> Parser::parse_text(const std::string &input, size_t &pos) {
    size_t start = pos;
    std::string text;

    auto is_line_start = [&]() -> bool {
        return pos == 0 || input[pos - 1] == '\n';
    };

    while (pos < input.size()) {
        char c = input[pos];
        if (pos > start && (c == '<' || c == '[' || c == '{' || (c == '=' && is_line_start()))) {
            break;  // Potential starter, stop text parsing
        }
        text += c;
        ++pos;
    }

    if (text.empty()) {
        return nullptr;  // No text collected
    }

    // Create TextElement with active=true (post-preprocess)
    return std::make_unique<elements::TextElement>(text, true, start, pos);
}

// Balance delimiters (stack for nested)
bool Parser::balance_delimiters(const std::string& open, const std::string& close, const std::string& input, size_t& pos, std::string& content) {
    int depth = 1;  // Start after first open
    size_t start_content = pos;
    while (pos < input.size()) {
        if (input.substr(pos, open.size()) == open) {
            depth++;
            pos += open.size();
        } else if (input.substr(pos, close.size()) == close) {
            depth--;
            pos += close.size();
            if (depth == 0) {
                content = input.substr(start_content, pos - close.size() - start_content);
                return true;
            }
        } else {
            pos++;
        }
    }
    return false;  // Unbalanced
}

}