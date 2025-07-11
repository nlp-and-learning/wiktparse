#pragma once
#include <memory>
#include "WikiLink.h"
#include "../BaseParser.h"

class WikiLinkParser: public BaseParser {
public:
    WikiLinkParser(const std::string& text, size_t pos) : BaseParser(text, pos) {}
    std::unique_ptr<WikiLink> parse() {
        if (!startsWithFrom("[[")) return nullptr;
        pos += 2;
        size_t startPos = pos;
        auto link = std::make_unique<WikiLink>();
        std::string currentPart;

        while (pos + 1 < text.size()) {
            // Detecting ]] termination without consuming the suffix
            if (text[pos] == ']' && text[pos + 1] == ']') {
                link->parts.push_back(std::move(currentPart));
                pos += 2;

                // Suffix parsing: ASCII letters only
                while (pos < text.size() && std::isalpha(static_cast<unsigned char>(text[pos]))) {
                    link->suffix.push_back(text[pos]);
                    ++pos;
                }
                break;
            }

            // New part after '|'
            if (text[pos] == '|') {
                link->parts.push_back(std::move(currentPart));
                currentPart.clear();
                ++pos;
                continue;
            }

            // Normal character (including \n, spaces, whatever)
            currentPart.push_back(text[pos]);
            ++pos;
        }

        // If there was no closure ]], treat as one part
        if (pos + 1 >= text.size()) {
            link->parts.clear();
            link->parts.push_back(text.substr(startPos));
            link->suffix.clear();
        }
        if (!link->parts.empty()) {
            link->target = link->parts[0];
            link->display = link->parts.back() + link->suffix;
        }
        return link;
    }
};
