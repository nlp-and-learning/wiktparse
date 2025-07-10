#include "MarkupParser.h"

#include <sstream>

#include "wikinodes/TemplateParser.h"
#include "wikinodes/WikiLinkParser.h"

std::unique_ptr<Markup> MarkupParser::parse(bool insideParam) {
    auto composite = std::make_unique<Markups>();
    std::ostringstream buffer;
    while (pos < text.size()) {
        if (insideParam) {
            if (text.compare(pos, 2, "}}") == 0 || text[pos] == '|' || text[pos] == '\n')
                break;
        }
        auto startFragment = specialAt();
        switch (startFragment) {
            case StartSpecial::Template: {
                TemplateParser templateParser(text, pos);
                composite->parts.push_back(templateParser.parse());
                templateParser.parse();
                pos = templateParser.getPos();
                break;
            }
            case StartSpecial::WikiLink: {
                WikiLinkParser wikiLinkParser(text, pos);
                composite->parts.push_back(wikiLinkParser.parse());
                pos = wikiLinkParser.getPos();
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
