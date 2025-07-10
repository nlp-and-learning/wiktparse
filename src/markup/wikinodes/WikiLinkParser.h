#pragma once
#include <memory>
#include "WikiLink.h"
#include "../BaseParser.h"

class WikiLinkParser: public BaseParser {
public:
    WikiLinkParser(const std::string& text, size_t pos) : BaseParser(text, pos) {}
    std::unique_ptr<WikiLink> parse() {
        if (!startsWithFrom("[[")) return nullptr;
        size_t startPos = pos;
        pos += 2;

        std::stringstream targetStream;
        std::stringstream displayStream;

        enum class State { Target, Display };
        State state = State::Target;

        while (pos + 1 < text.size()) {
            char c = text[pos];
            char next = text[pos + 1];

            // WikiLink end
            if (c == ']' && next == ']') {
                pos += 2;
                break;
            }

            // Not allowed: nested [[
            if (c == '[' && next == '[') {
                return nullptr;
            }

            // Separator
            if (c == '|' && state == State::Target) {
                ++pos;
                state = State::Display;
                continue;
            }

            // Addition chars
            if (state == State::Target) {
                targetStream << c;
            } else {
                displayStream << c;
            }

            ++pos;
        }

        if (pos == startPos + 2) return nullptr; // empty [[ ]]

        // Suffice (ascii stream after ]])
        std::string suffix;
        while (pos < text.size() && isalpha(text[pos])) {
            suffix += text[pos++];
        }

        auto link = std::make_unique<WikiLink>();
        link->target = targetStream.str();
        link->display = (state == State::Display ? displayStream.str() : link->target) + suffix;

        return link;
    }
};
