#include "MarkupParser.h"

#include <sstream>

#include "tags/TagParser.h"
#include "wikinodes/HeaderParser.h"
#include "wikinodes/TemplateParser.h"
#include "wikinodes/WikiLinkParser.h"

//asParamValue: 0: none; 1: unnamed; 2: named->trim
std::unique_ptr<Markup> MarkupParser::parse(int asParamValue) {
    auto composite = std::make_unique<Markups>();
    std::ostringstream buffer;
    if (asParamValue == 2)
        Whitespace::skipWhiteBreaks(text, pos);
    while (pos < text.size()) {
        if (asParamValue > 0) {
            if (text.compare(pos, 2, "}}") == 0 || text.compare(pos, 2, "]]") == 0 ||  text[pos] == '|')
                break;
            if (asParamValue == 2 && text[pos] == '\n')
                break;
        }
        auto startFragment = specialAt();
        if (startFragment!=StartSpecial::Other) {
            if (!buffer.str().empty()) {
                composite->parts.push_back(std::make_unique<RichText>(buffer.str()));
                buffer.str("");
            }
        }
        switch (startFragment) {
            case StartSpecial::Tag: {
                TagParser tagParser(text, pos);
                composite->parts.push_back(tagParser.parse());
                pos = tagParser.getPos();
                break;
            }
            case StartSpecial::Template: {
                TemplateParser templateParser(text, pos);
                composite->parts.push_back(templateParser.parse());
                pos = templateParser.getPos();
                break;
            }
            case StartSpecial::WikiLink: {
                WikiLinkParser wikiLinkParser(text, pos);
                composite->parts.push_back(wikiLinkParser.parse());
                pos = wikiLinkParser.getPos();
                break;
            }
            case StartSpecial::Header: {
                HeaderParser headerParser(text, pos);
                composite->parts.push_back(headerParser.parse());
                pos = headerParser.getPos();
                break;
            }
            default:buffer << text[pos++];
        }
    }
    if (!buffer.str().empty())
        composite->parts.push_back(std::make_unique<RichText>(buffer.str()));
    if (composite->parts.empty())
        return std::make_unique<RichText>("");
    else if (composite->parts.size() == 1)
        return std::move(composite->parts[0]);
    else
        return composite;
}
