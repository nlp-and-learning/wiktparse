#pragma once
#include "Tag.h"
#include "TagParser.h"

enum class StartTagSpecial {
    CommentOpen,
    CommentClose,
    ConsiderTag,
    Other,
};

class TagStreamParser {
    const std::string& text;
    size_t pos;
    bool startsWithFrom(const std::string& prefix) { //todo remove redundant
        return text.size() >= pos + prefix.size() &&
               text.compare(pos, prefix.size(), prefix) == 0;
    }
    StartTagSpecial specialTagAt() {
        switch (text[pos]) {
            case '<':
            {
                if (startsWithFrom("<!--"))
                    return StartTagSpecial::CommentOpen;
                else
                    return StartTagSpecial::ConsiderTag;
            }
            case '-': {
                if (startsWithFrom("-->"))
                    return StartTagSpecial::CommentClose;
                else
                    return StartTagSpecial::Other;
            }
            default: return StartTagSpecial::Other;
        }
    }
    CommentStatus commentStatus = CommentStatus::Displayed;
public:
    TagStreamParser(const std::string& text, size_t pos) : text(text) , pos(pos) {}
    std::unique_ptr<TagBase> parse() {
        auto composite = std::make_unique<TagStream>();
        std::ostringstream ss;
        while (pos < text.size()) {
            auto spec = specialTagAt();
            if (spec == StartTagSpecial::CommentOpen) {
                if (commentStatus == CommentStatus::Displayed) {
                    auto displayed = ss.str();
                    composite->parts.emplace_back(std::make_unique<TagFragment>(displayed, commentStatus));
                    ss.str("");
                    commentStatus = CommentStatus::Commented;
                }
            } else if (spec == StartTagSpecial::CommentClose) {
                if (commentStatus == CommentStatus::Commented) {
                    for (int i=0; i<3; i++) {
                        ss << text[pos];
                        pos++;
                    }
                    auto commented = ss.str();
                    composite->parts.emplace_back(std::make_unique<TagFragment>(commented, commentStatus));
                    ss.str("");
                    commentStatus = CommentStatus::Displayed;
                    if (pos >= text.size()) break;
                }
            } else if (spec == StartTagSpecial::ConsiderTag) {
                TagParser tagParser(text, pos);
                auto tag = tagParser.parse();
                if (tag->type!=TagType::Invalid) {
                    std::string str = tag->dump(DUMP_ALL);
                }
            }
            ss << text[pos];
            pos++;
        }
        if (!ss.str().empty()) {
            auto rest = ss.str();
            composite->parts.emplace_back(std::make_unique<TagFragment>(rest, commentStatus));
        }
        if (composite->parts.empty())
            return std::make_unique<TagFragment>("", CommentStatus::Displayed);
        else if (composite->parts.size() == 1)
            return std::move(composite->parts[0]);
        else
            return composite;
    }
};
