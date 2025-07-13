// parser/Parser.h
#ifndef PARSER_H
#define PARSER_H

#ifdef UNIT_TEST
#include <gtest/gtest_prod.h>
#endif
#include <cstring>
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
#include "../elements/TextElement.h"
#include "../factory/TagFactory.h"
#include "ParserError.h"

namespace parser {

    enum class StarterType { NONE, TAG, TEMPLATE, WIKILINK, EXTERNAL_LINK, HEADER, TEXT };

    class Parser {
    public:
        Parser(const std::string& input_text);
        std::vector<std::unique_ptr<elements::WikiElement>> parse();

    private:
        std::string input;
        size_t pos = 0;

        // Detect starter at current pos
        StarterType detect_starter(size_t& start_pos);

        // Subparsers now take input and &pos, return object and advance pos
        std::unique_ptr<elements::Tag> parse_tag(const std::string& input, size_t& pos);
        std::unique_ptr<elements::Template> parse_template(const std::string& input, size_t& pos);
        std::unique_ptr<elements::WikiLink> parse_wikilink(const std::string& input, size_t& pos);
        std::unique_ptr<elements::ExternalLink> parse_external_link(const std::string& input, size_t& pos);

        static bool isWikiSpace(char c);
        size_t trimmedEolPos(const std::string &input, size_t pos);
        std::unique_ptr<elements::Header> parse_header(const std::string& input, size_t& pos);

        // Parse text until next starter
        std::unique_ptr<elements::TextElement> parse_text(const std::string& input, size_t& pos);

        // Helper for balanced delimiters (for nested {{ }})
        bool balance_delimiters(const std::string& open, const std::string& close, const std::string& input, size_t& pos, std::string& content);
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

#endif //PARSER_H