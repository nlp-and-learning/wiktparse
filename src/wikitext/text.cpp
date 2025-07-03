#include "text.h"

#include "TemplateParser.h"

std::unique_ptr<WikiLink> parseWikiLink(const std::string& text, size_t& pos) {
    size_t start = pos;
    pos += 2;
    std::ostringstream target, display;
    bool hasPipe = false;
    while (pos < text.size()) {
        if (text.compare(pos, 2, "]]") == 0) {
            pos += 2;
            break;
        }
        if (text[pos] == '|' && !hasPipe) {
            hasPipe = true;
            ++pos;
            continue;
        }
        if (hasPipe) display << text[pos];
        else target << text[pos];
        ++pos;
    }
    auto link = std::make_unique<WikiLink>();
    link->target = target.str();
    if (hasPipe) link->displayText = display.str();
    return link;
}

enum class StartFragment {
    Plain,
    Function,
    Template,
    WikiLink,
};

StartFragment getStartFragment(const std::string& text, size_t& pos) {
    if (text[pos]!='[' && text[pos]!='{')
        return StartFragment::Plain;

    if (text.compare(pos, 3, "{{#") == 0) {
        return StartFragment::Function;
    }
    if (text.compare(pos, 2, "{{") == 0) {
        return StartFragment::Template;
    }
    if (text.compare(pos, 2, "[[") == 0) {
        return StartFragment::WikiLink;
    }
    return StartFragment::Plain;
}

std::unique_ptr<CompositeText> parseCompositeText(const std::string& text, size_t& pos, bool insideParam) {
    auto composite = std::make_unique<CompositeText>();
    std::ostringstream buffer;
    while (pos < text.size()) {
        if (insideParam) {
            if (text.compare(pos, 2, "}}") == 0 || text[pos] == '|' || text[pos] == '\n')
                break;
        }
        auto startFragment = getStartFragment(text, pos);
        if (startFragment != StartFragment::Plain) {
            if (!buffer.str().empty()) {
                composite->parts.push_back(std::make_unique<PlainText>(buffer.str()));
                buffer.str("");
            }
        }
        switch (startFragment) {
            case StartFragment::Function:
                composite->parts.push_back(TemplateParser::parseParserFunction(text, pos));break;
            case StartFragment::Template:
                composite->parts.push_back(TemplateParser::parseTemplate(text, pos));break;
            case StartFragment::WikiLink:
                composite->parts.push_back(parseWikiLink(text, pos));break;
            default:buffer << text[pos++];
        }
    }
    if (!buffer.str().empty())
        composite->parts.push_back(std::make_unique<PlainText>(buffer.str()));
    return composite;
}
