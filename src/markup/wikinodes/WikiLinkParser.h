#pragma once
#include <memory>
#include "WikiLink.h"
#include "../MarkupParser.h"

class WikiLinkParser: public BaseParser {
public:
    WikiLinkParser(const std::string& text, size_t pos) : BaseParser(text, pos) {}
    std::unique_ptr<WikiLink> parse() {
        if (!startsWithFrom("[[")) return nullptr;
        pos += 2;
        size_t startPos = pos;
        auto link = std::make_unique<WikiLink>();

        while (pos + 1 < text.size()) {
            // Detecting ]] termination without consuming the suffix
            if (text[pos] == ']' && text[pos + 1] == ']') {
                pos += 2;

                // Suffix parsing: ASCII letters only
                while (pos < text.size() && std::isalpha(static_cast<unsigned char>(text[pos]))) {
                    link->suffix.push_back(text[pos]);
                    ++pos;
                }
                break;
            }

            // Parse one part with subparser
            MarkupParser subparser(text, pos);
            auto part = subparser.parse(CalledFrom::WikiLink);
            pos = subparser.getPos();

            if (part) {
                link->parts.emplace_back(std::move(part));
            }

            // If we are on pipe, skip it
            if (pos < text.size() && text[pos] == '|') {
                ++pos;
            }
        }

        if (!link->parts.empty()) {
            link->target = link->parts[0]->dump();
        }
        return link;
    }
};
