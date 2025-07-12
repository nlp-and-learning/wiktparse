#include "MarkupParser.h"

#include <sstream>

#include "tags/TagParser.h"
#include "wikinodes/HeaderParser.h"
#include "wikinodes/TemplateParser.h"
#include "wikinodes/WikiLinkParser.h"

std::unique_ptr<Markup> MarkupParser::parse(CalledFrom asParamValue) {
    auto composite = std::make_unique<Markups>();
    std::ostringstream buffer;
    if (asParamValue == CalledFrom::NamedParam)
        Whitespace::skipWhiteBreaks(text, pos);
    while (pos < text.size()) {
        if (asParamValue != CalledFrom::Top) {
            if (asParamValue == CalledFrom::WikiLink) {
                if (text.compare(pos, 2, "]]") == 0 ||  text[pos] == '|')
                    break;
            }
            else {
                if (text.compare(pos, 2, "}}") == 0 ||  text[pos] == '|')
                    break;
            }
            if (asParamValue == CalledFrom::NamedParam && text[pos] == '\n')
                break;
        }
        auto startFragment = specialAt();
        switch (startFragment) {
            case StartSpecial::Tag: {
                TagParser tagParser(text, pos);
                auto tag = tagParser.parse();
                if (tag->type == TagType::Invalid) {
                    buffer << text[pos++];
                } else {
                    if (!buffer.str().empty()) {
                        composite->parts.push_back(std::make_unique<RichText>(buffer.str()));
                        buffer.str("");
                    }
                    composite->parts.push_back(std::move(tag));
                    pos = tagParser.getPos();
                }
                break;
            }
            case StartSpecial::Template: {
                TemplateParser templateParser(text, pos);
                auto templ = templateParser.parse();
                if (templ->invalid) {
                    buffer << text[pos++];
                } else {
                    if (!buffer.str().empty()) {
                        composite->parts.push_back(std::make_unique<RichText>(buffer.str()));
                        buffer.str("");
                    }
                    composite->parts.push_back(std::move(templ));
                    pos = templateParser.getPos();
                }
                break;
            }
            case StartSpecial::WikiLink: {
                WikiLinkParser wikiLinkParser(text, pos);
                auto link = wikiLinkParser.parse();
                if (!buffer.str().empty()) {
                    composite->parts.push_back(std::make_unique<RichText>(buffer.str()));
                    buffer.str("");
                }
                composite->parts.push_back(std::move(link));
                pos = wikiLinkParser.getPos();
                break;
            }
            case StartSpecial::Header: {
                HeaderParser headerParser(text, pos);
                auto header = headerParser.parse();
                if (header->level == 0) {
                    buffer << text[pos++];
                } else {
                    if (!buffer.str().empty()) {
                        composite->parts.push_back(std::make_unique<RichText>(buffer.str()));
                        buffer.str("");
                    }
                    composite->parts.push_back(std::move(header));
                    pos = headerParser.getPos();
                }
                break;
            }
            default:buffer << text[pos++];
        }
    }
    if (!buffer.str().empty())
        composite->parts.push_back(std::make_unique<RichText>(buffer.str()));
    if (composite->parts.empty())
        return nullptr;
    else if (composite->parts.size() == 1)
        return std::move(composite->parts[0]);
    else
        return composite;
}
