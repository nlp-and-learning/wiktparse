#pragma once
#include "CompositeText.h"
#include "../tags/Parser.h"

class TextParser: public Parser {
    CommentStatus commentStatus = CommentStatus::Displayed;
public:
    TextParser(const std::string& text, size_t pos) : Parser(text, pos) {}
    std::unique_ptr<Fragment> parse() {
        auto composite = std::make_unique<CompositeText>();
        std::ostringstream ss;
        while (pos < text.size()) {
            auto spec = specialAt();
            if (spec == StartSpecial::CommentOpen) {
                if (commentStatus == CommentStatus::Displayed) {
                    auto displayed = ss.str();
                    composite->parts.emplace_back(std::make_unique<TextFragment>(displayed, commentStatus));
                    ss.str("");
                    commentStatus = CommentStatus::Commented;
                }
            } else if (spec == StartSpecial::CommentClose) {
                if (commentStatus == CommentStatus::Commented) {
                    for (int i=0; i<3; i++) {
                        ss << text[pos];
                        pos++;
                    }
                    auto commented = ss.str();
                    composite->parts.emplace_back(std::make_unique<TextFragment>(commented, commentStatus));
                    ss.str("");
                    commentStatus = CommentStatus::Displayed;
                    if (pos >= text.size()) break;
                }
            }
            ss << text[pos];
            pos++;
        }
        if (!ss.str().empty()) {
            auto rest = ss.str();
            composite->parts.emplace_back(std::make_unique<TextFragment>(rest, commentStatus));
        }
        if (composite->parts.empty())
            return std::make_unique<TextFragment>("", CommentStatus::Displayed);
        else if (composite->parts.size() == 1)
            return std::move(composite->parts[0]);
        else
            return composite;
    }
};
