// parser/Parser.h
#ifndef PARSER_H
#define PARSER_H

#ifdef UNIT_TEST
#include <gtest/gtest_prod.h>
#endif
#include <string>
#include <vector>
#include <stack>
#include <memory>  // For unique_ptr and stack
#include <regex>
#include "../elements/WikiElement.h"
#include "../elements/TaggedContent.h"
#include "../elements/Template.h"
#include "../elements/Header.h"
#include "../elements/WikiLink.h"
#include "../elements/ExternalLink.h"
#include "../elements/Tag.h"
#include "../elements/TextFragment.h"
#include "../factory/TagFactory.h"
#include "ParserError.h"

namespace parser {

class Parser {
public:
    // Constructor
    Parser(const std::string& input_text);

    // Main parsing method
    std::vector<std::unique_ptr<elements::WikiElement>> parse();

    // Preprocessing (static or member)
    static std::vector<std::unique_ptr<elements::TextFragment>> preprocess(const std::string& wiki_text);

private:
    std::string input;
    size_t pos;
    std::stack<std::unique_ptr<elements::TaggedContent>> stack;
    size_t last_newline;  // To track the beginning of the line for headers

    // Lexer: Get the next token
    struct Token {
        enum class TokenType { TEXT, TAG, HEADER, TEMPLATE, WIKILINK, EXTERNAL_LINK, INVALID };
        TokenType type;
        std::string value;
        size_t start;
    };
    Token next_token();

    // Subparsers (private methods)
    std::unique_ptr<elements::Tag> parse_tag(const std::string& value);
    std::unique_ptr<elements::Header> parse_header(const std::string& value);
    std::unique_ptr<elements::Template> parse_template(const std::string& value);
    std::unique_ptr<elements::WikiLink> parse_wikilink(const std::string& value);
    std::unique_ptr<elements::ExternalLink> parse_external_link(const std::string& value);

    // Auxiliary: Balance delimiters (for nesting)
    std::string extract_balanced(const std::string& delim_open, const std::string& delim_close, size_t start_pos);

    // Handle unmatched stack etc.
    void handle_unmatched();
#ifdef UNIT_TEST
    FRIEND_TEST(TemplateParsingTest, ParseSimpleTemplate);
    FRIEND_TEST(HeaderParsingTest, ParseBalancedHeaderLevel3);
    FRIEND_TEST(HeaderParsingTest, ParseUnbalancedHeaderMinLevel);
    FRIEND_TEST(WikiLinkParsingTest, ParseSimpleWikiLink);
    FRIEND_TEST(WikiLinkParsingTest, ParseWikiLinkWithDisplay);
    FRIEND_TEST(WikiLinkParsingTest, ParseWikiLinkWithSuffix);
    FRIEND_TEST(WikiLinkParsingTest, ParseWikiLinkWithDisplayAndSuffix);
    FRIEND_TEST(ExternalLinkParsingTest, ParseSimpleExternalLinkNoSpace);
    FRIEND_TEST(ExternalLinkParsingTest, ParseExternalLinkWithSpaceAndDescription);
    FRIEND_TEST(TagParsingTest, ParseOpeningTag);
    FRIEND_TEST(TagParsingTest, ParseClosingTag);
    FRIEND_TEST(TagParsingTest, ParseSelfClosingTagWithSpaces);
    FRIEND_TEST(TagParsingTest, ParseTagWithDoubleQuotedAttributes);
    FRIEND_TEST(TagParsingTest, ParseTagWithMixedQuotedAttributes);
    FRIEND_TEST(TagParsingTest, ParseInvalidNonTagInText);
    FRIEND_TEST(TagParsingTest, ParseValidTagInText);
#endif
};

}  // namespace parser

#endif  // PARSER_H