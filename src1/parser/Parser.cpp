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

namespace parser {

// Constructor
Parser::Parser(const std::string& input_text) : input(input_text), pos(0), last_newline(0) {}

// Main parsing method (simple implementation to start with)
std::vector<std::unique_ptr<elements::WikiElement>> Parser::parse() {
    auto root = std::make_unique<elements::TaggedContent>(nullptr, 0, 0);  // Dummy root
    stack.push(root.get());
    auto current = stack.top();

    while (pos < input.size()) {
        Token token = next_token();
        switch (token.type) {
            case Token::TokenType::TAG: {
                auto tag = parse_tag(token.value);
                if (tag->is_opening()) {
                    auto content = std::make_unique<elements::TaggedContent>(std::move(tag), token.start, 0);
                    elements::TaggedContent* content_ptr = content.get();
                    current->add_child(std::move(content));
                    stack.push(content_ptr);
                } else if (tag->is_closing()) {
                    if (!stack.empty() && stack.top()->get_opening_tag() && stack.top()->get_opening_tag()->get_name() == tag->get_name()) {
                        stack.top()->set_closing_tag(std::move(tag));
                        stack.top()->set_end_pos(pos);  // Set end_pos
                        stack.pop();
                    } else {
                        throw ParserError("Unmatched closing tag");
                    }
                } else {
                    current->add_child(std::move(tag));
                }
                break;
            }
            case Token::TokenType::TEMPLATE: {
                auto templ = parse_template(token.value);
                templ->set_start_pos(token.start);
                templ->set_end_pos(pos);
                current->add_child(std::move(templ));
                break;
            }
            // Add handling for other types: HEADER, WIKILINK, etc.
            case Token::TokenType::TEXT: {
                current->add_child(std::make_unique<elements::TextElement>(token.value, true, token.start, pos));
                break;
            }
            default:
                throw ParserError("Invalid token");
        }
    }
    handle_unmatched();
    return stack.top()->take_content();
}

// Preprocessing (comments and <nowiki>)
std::vector<std::unique_ptr<elements::TextElement>> Parser::preprocess(const std::string& wiki_text) {
    std::vector<std::unique_ptr<elements::TextElement>> fragments;
    size_t p = 0;
    std::regex comment_regex(R"(<!--[\s\S]*?-->)");
    std::regex nowiki_open_regex(R"(<nowiki>)");
    std::regex nowiki_close_regex(R"(</nowiki>)");

    while (p < wiki_text.size()) {
        // Skip comments
        std::smatch match;
        if (std::regex_search(wiki_text.cbegin() + p, wiki_text.cend(), match, comment_regex)) {
            fragments.push_back(std::make_unique<elements::TextElement>(wiki_text.substr(p, match.position()), true, p, p + match.position()));
            p += match.position() + match.length();
        } else if (std::regex_search(wiki_text.cbegin() + p, wiki_text.cend(), match, nowiki_open_regex)) {
            // Add before <nowiki>
            fragments.push_back(std::make_unique<elements::TextElement>(wiki_text.substr(p, match.position()), true, p, p + match.position()));
            p += match.position() + match.length();
            // Search </nowiki>
            if (std::regex_search(wiki_text.cbegin() + p, wiki_text.cend(), match, nowiki_close_regex)) {
                fragments.push_back(std::make_unique<elements::TextElement>(wiki_text.substr(p, match.position()), false, p, p + match.position()));
                p += match.position() + match.length();
            } else {
                throw ParserError("Unmatched <nowiki>");
            }
        } else {
            fragments.push_back(std::make_unique<elements::TextElement>(wiki_text.substr(p), true, p, wiki_text.size()));
            p = wiki_text.size();
        }
    }
    return fragments;
}

// Lexer: next_token (simple, for starters - use regex for patterns)
Parser::Token Parser::next_token() {
    std::smatch match;
    std::regex tag_regex(R"(<[^>]+>)");
    std::regex template_regex(R"(\{\{[\s\S]*?\}\})");

    if (std::regex_search(input.cbegin() + pos, input.cend(), match, tag_regex)) {
        size_t start = pos + match.position();
        pos = start + match.length();
        return {Token::TokenType::TAG, match[0], start};
    } else if (std::regex_search(input.cbegin() + pos, input.cend(), match, template_regex)) {
        size_t start = pos + match.position();
        pos = start + match.length();
        return {Token::TokenType::TEMPLATE, match[0], start};
    } else {
        // Plain text to the next potential token
        size_t end = input.size();
        // Search for the next special
        if (std::regex_search(input.cbegin() + pos, input.cend(), match, tag_regex)) {
            end = pos + match.position();
        }
        std::string text = input.substr(pos, end - pos);
        pos = end;
        return {Token::TokenType::TEXT, text, pos - text.size()};
    }
    // Add more cases
}

// Subparser for tag
std::unique_ptr<elements::Tag> Parser::parse_tag(const std::string& value) {
    // Simple implementation: extract name, attributes, type
    std::string name;
    std::unordered_map<std::string, std::string> attrs;
    elements::TagType type = elements::TagType::INVALID;
    bool syntactically_valid = false;  // Only syntax check here

    // Use regex to parse <tag attr="val" />
    std::regex tag_pattern(R"(<(/?)(\w+)([^>]*)>)");
    std::smatch m;
    if (std::regex_match(value, m, tag_pattern)) {
        bool is_closing = !m[1].str().empty();
        name = m[2].str();
        std::string attrs_str = m[3].str();
        bool is_self_closing = false;
        if (!attrs_str.empty() && attrs_str.back() == '/') {
            is_self_closing = true;
            attrs_str.pop_back();
        }


        // Determine type
        if (is_closing && is_self_closing) {
            type = elements::TagType::INVALID;  // Closing and self-closing simultaneously invalid
        } else if (is_closing) {
            type = elements::TagType::CLOSING;
        } else if (is_self_closing) {
            type = elements::TagType::SELF_CLOSING;
        } else {
            type = elements::TagType::OPENING;
        }

        // Parse attributes from attrs_str
        std::string attr_pattern_str = R"(\s*(\w+)\s*=\s*)";
        attr_pattern_str += "(\"([^\"]*)\"|'([^']*)'|([^ \\t>]+))";
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
            attrs[key] = val;
        }

        syntactically_valid = (type != elements::TagType::INVALID);
    }  // If no match (e.g., no >), remains invalid and returns Tag with INVALID

    return factory::TagFactory::get_instance().create_tag(name, attrs, type, syntactically_valid);
}

// Subparser for template (for testing)
std::unique_ptr<elements::Template> Parser::parse_template(const std::string& value) {
    // Remove {{ i }}
    std::string inner = value.substr(2, value.size() - 4);

    // Split by |
    std::vector<std::string> parts;
    size_t p = 0;
    int depth = 0;
    std::string current_part;
    while (p < inner.size()) {
        if (inner[p] == '{' || inner[p] == '[') depth++;
        else if (inner[p] == '}' || inner[p] == ']') depth--;
        else if (inner[p] == '|' && depth == 0) {
            parts.push_back(current_part);
            current_part.clear();
            p++;
            continue;
        }
        current_part += inner[p];
        p++;
    }
    if (!current_part.empty()) parts.push_back(current_part);

    if (parts.empty()) throw ParserError("Empty template");

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

    return std::make_unique<elements::Template>(name, params, 0, 0);  // Pozycje ustaw w callerze
}

// Implementations of other sub-parsers (empty at startup)
 std::unique_ptr<elements::Header> Parser::parse_header(const std::string& value) {
    // Count leading =
    size_t leading = 0;
    while (leading < value.size() && value[leading] == '=') {
        ++leading;
    }

    // Count trailing =
    size_t trailing = 0;
    size_t pos = value.size();
    while (pos > 0 && value[pos - 1] == '=') {
        --pos;
        ++trailing;
    }

    // If missing = invalid
    if (leading == 0 && trailing == 0) {
        return nullptr;
    }

    // Level to min(leading, trailing)
    int level = std::min(leading, trailing);

    // If level <1 or >6, invalid (optional, MediaWiki limit 6)
    if (level < 1 || level > 6) {
        return nullptr;
    }

    // Title to substr between, including extra = if unbalanced
    std::string title = value.substr(level, value.size() - 2 * level);

    // Trim whitespace z title (MediaWiki behavior)
    size_t start = title.find_first_not_of(" \t");
    if (start == std::string::npos) {
        title.clear();  // Tylko whitespace
    } else {
        size_t end = title.find_last_not_of(" \t");
        title = title.substr(start, end - start + 1);
    }

    return std::make_unique<elements::Header>(level, title, 0, 0);  // Set positions in the caller
}

std::unique_ptr<elements::WikiLink> Parser::parse_wikilink(const std::string& value) {
    // We assume that the value starts with [[ and ends with ]] + optional suffix
    if (value.size() < 4 || value.substr(0, 2) != "[[" || value.find("]]") == std::string::npos) {
        return nullptr;  // Invalid format
    }

    size_t close_pos = value.find("]]", 2);
    std::string inner = value.substr(2, close_pos - 2);
    std::string suffix = value.substr(close_pos + 2);

    // Check suffix: only ASCII letters (isalpha, a-zA-Z)
    bool valid_suffix = !suffix.empty();
    for (char c : suffix) {
        if (!std::isalpha(static_cast<unsigned char>(c))) {  // not an ASCII character
            valid_suffix = false;
            break;
        }
    }
    if (!valid_suffix) {
        suffix.clear();
    }

    // Parse inner: target and optional display
    std::string target;
    std::optional<std::string> display;
    size_t pipe_pos = inner.find('|');
    if (pipe_pos != std::string::npos) {
        target = inner.substr(0, pipe_pos);
        display = inner.substr(pipe_pos + 1);
    } else {
        target = inner;
    }

    // Dołącz suffix do display
    if (!suffix.empty()) {
        if (display.has_value()) {
            display = display.value() + suffix;
        } else {
            display = target + suffix;
        }
    }

    // Positions: full value
    size_t start_pos = 0;
    size_t end_pos = value.size();

    return std::make_unique<elements::WikiLink>(target, display, start_pos, end_pos);
}

std::unique_ptr<elements::ExternalLink> Parser::parse_external_link(const std::string& value) {
    // Assume value starts with [ and ends with ], no suffix
    if (value.size() < 3 || value[0] != '[' || value.back() != ']') {
        return nullptr;  // Invalid format
    }

    std::string inner = value.substr(1, value.size() - 2);

    // Find first space - before is url, after is display
    size_t space_pos = inner.find(' ');
    std::string url;
    std::optional<std::string> display;
    if (space_pos != std::string::npos) {
        url = inner.substr(0, space_pos);
        std::string desc = inner.substr(space_pos + 1);

        // Trim leading/trailing spaces from display (handle multiple spaces)
        size_t start = desc.find_first_not_of(" \t");
        if (start != std::string::npos) {
            size_t end = desc.find_last_not_of(" \t");
            display = desc.substr(start, end - start + 1);
        }
    } else {
        url = inner;
    }

    // URL validation: scheme (ASCII letters, optional digits) followed by "://"
    std::regex url_scheme_regex(R"([a-zA-Z][a-zA-Z0-9]*://)");
    std::smatch match;
    if (!std::regex_search(url, match, url_scheme_regex) || match.position() != 0) {
        return nullptr;  // Invalid URL scheme
    }

    // Positions: full value
    size_t start_pos = 0;
    size_t end_pos = value.size();

    return std::make_unique<elements::ExternalLink>(url, display, start_pos, end_pos);
}

// Balance delimiters (for nesting)
std::string Parser::extract_balanced(const std::string& delim_open, const std::string& delim_close, size_t start_pos) {
    // TODO: Implement balancing
    return "";
}

// Handle unmatched
void Parser::handle_unmatched() {
    if (stack.size() > 1) {  // Ignore dummy root (size==1 is OK)
        throw ParserError("Unmatched tags");
    }
}

}  // namespace parser