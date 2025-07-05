#include "old_text.h"

#include <format>
#include <iostream>

#include "old_TemplateParser.h"

std::unique_ptr<old_WikiLink> old_parseWikiLink(const std::string& text, size_t& pos) {
    if (text.compare(pos, 2, "[[") != 0)
        throw std::runtime_error("Expected [[ at pos " + std::to_string(pos));

    pos += 2;
    std::ostringstream current;
    auto link = std::make_unique<old_WikiLink>();

    while (pos < text.size()) {
        if (text.compare(pos, 2, "]]") == 0) {
            if (link->target.empty())
                link->target = current.str();
            else
                link->parameters.push_back(current.str());
            pos += 2;
            break;
        } else if (text[pos] == '|') {
            if (link->target.empty())
                link->target = current.str();
            else
                link->parameters.push_back(current.str());
            current.str("");
            ++pos;
        } else {
            current << text[pos++];
        }
    }
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

std::unique_ptr<old_TextFragment> old_parseCompositeText(const std::string& text, size_t& pos, bool insideParam) {
    auto composite = std::make_unique<old_CompositeText>();
    std::ostringstream buffer;
    while (pos < text.size()) {
        if (insideParam) {
            if (text.compare(pos, 2, "}}") == 0 || text[pos] == '|' || text[pos] == '\n')
                break;
        }
        auto startFragment = getStartFragment(text, pos);
        if (startFragment != StartFragment::Plain) {
            if (!buffer.str().empty()) {
                composite->parts.push_back(std::make_unique<old_PlainText>(buffer.str()));
                buffer.str("");
            }
        }
        switch (startFragment) {
            case StartFragment::Function:
                composite->parts.push_back(old_TemplateParser::parseParserFunction(text, pos));break;
            case StartFragment::Template:
                composite->parts.push_back(old_TemplateParser::parseTemplate(text, pos));break;
            case StartFragment::WikiLink:
                composite->parts.push_back(old_parseWikiLink(text, pos));break;
            default:buffer << text[pos++];
        }
    }
    if (!buffer.str().empty())
        composite->parts.push_back(std::make_unique<old_PlainText>(buffer.str()));
    if (composite->parts.empty())
        return std::make_unique<old_PlainText>("");
    else if (composite->parts.size() == 1)
        return std::move(composite->parts[0]);
    else
        return composite;
}
