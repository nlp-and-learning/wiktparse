#pragma once
#include "CompositeText.h"
#include "Parser.h"

class TextParser: public Parser {
    CommentStatus commentStatus = CommentStatus::Displayed;
public:
    TextParser(const std::string& text, size_t pos) : Parser(text, pos) {}
    std::unique_ptr<Fragment> parse() {
        auto composite = std::make_unique<CompositeText>();
        std::ostringstream ss;
        size_t outBreaks = 0;
        size_t outSpaces = 0;
        while (true) {
            auto p = skipWhiteBreaks();
            if (pos >= text.size()) break;
            if (commentStatus == CommentStatus::Displayed) {
                outBreaks = 0;
                outSpaces = 0;
            }
            if (p.first > 1) {
                std::string breaks(p.first - 1, '\n');
                if (commentStatus == CommentStatus::Displayed)
                    outBreaks = p.first - 1;
                ss << breaks;
            } else if (p.first + p.second > 0) {
                if (commentStatus == CommentStatus::Displayed)
                    outSpaces = 1;
                ss << ' ';
            }
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
                    auto p1 = skipWhiteBreaks();
                    if (p1.first > 0 && outBreaks > 0 || p1.first > 1 && outBreaks == 0) {
                        std::string breaks(p1.first, '\n');
                        ss << breaks;
                    } else if (p1.first + p1.second > 0) {
                        if (outSpaces==0)
                            ss << ' ';
                    }
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
