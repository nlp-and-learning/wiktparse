#include "Preprocessor.h"

#include <algorithm>
#include <cassert>

namespace preprocessor {
    size_t Preprocessor::len(const std::string &marker) {
        return marker.size();
    }

    int Preprocessor::findMarker(const std::vector<MarkerPos> &pairs, const std::string &marker, int start) {
        for (int i = start; i < pairs.size(); i++) {
            if (pairs[i].second == marker)
                return i;
        }
        return pairs.size();
    }

    std::vector<preprocessor::Preprocessor::MarkerPos> Preprocessor::getMarkerPos(const std::string &input) {
        std::vector<MarkerPos> markersPos;

        auto add_all = [&](const std::string& marker) {
            size_t pos = 0;
            while ((pos = input.find(marker, pos)) != std::string::npos) {
                markersPos.emplace_back(pos, marker);
                pos += 1;
            }
        };

        add_all("<!--");
        add_all("-->");
        add_all("<nowiki>");
        add_all("</nowiki>");

        std::sort(markersPos.begin(), markersPos.end(),
              [](const MarkerPos& a, const MarkerPos& b) { return a.first < b.first; });

        return markersPos;
    }

    std::vector<Preprocessor::WikiSegment> Preprocessor::findWikiSegments(const std::string& text,
        const std::vector<MarkerPos> &pairs) {
        size_t textLen = text.size();
        std::vector<WikiSegment> segments;
        int startPos = 0;
        size_t startSegment = 0;
        while (startPos < pairs.size()) {
            auto &tag = pairs[startPos];
            if (tag.second == "<nowiki>") {
                auto closingPos = findMarker(pairs, "</nowiki>", startPos + 1);
                if (closingPos < pairs.size()) {
                    if (pairs[startPos].first > startSegment)
                        segments.emplace_back(startSegment, pairs[startPos].first, true);
                    segments.emplace_back(pairs[startPos].first + len("<nowiki>"), pairs[closingPos].first, false);
                    startSegment = pairs[closingPos].first + len("</nowiki>");
                    startPos = closingPos + 1;
                }
                else
                    startPos++;
            }
            else if (tag.second == "<!--") {
                assert(pairs[startPos].first >= startSegment);
                if (pairs[startPos].first > startSegment)
                    segments.emplace_back(startSegment, pairs[startPos].first, true);
                auto closingPos = findMarker(pairs, "-->", startPos + 1);
                if (closingPos < pairs.size()) {
                    size_t commentEnd = pairs[closingPos].first + len("-->");
                    // Check if the comment is alone on a line
                    if (isCommentAloneOnLine(text, pairs[startPos].first, commentEnd)) {
                        // Skip one trailing newline if present
                        if (commentEnd < textLen && text[commentEnd] == '\n')
                            commentEnd++;
                    }
                    startSegment = commentEnd;
                }
                else
                    startSegment = textLen;
                if (closingPos >= pairs.size() - 1)
                    break;
                startPos = closingPos + 1;
            }
            else
                startPos++;
        }
        assert(textLen >= startSegment);
        if (textLen > startSegment)
            segments.emplace_back(startSegment, textLen, true);
        return segments;
    }

    std::vector<std::unique_ptr<TextElement>> Preprocessor::toTextElements(const std::string &text,
        const std::vector<WikiSegment> &segments) {
        std::vector<std::unique_ptr<TextElement>> text_elements;
        for (auto &seg : segments) {
            auto substr = text.substr(seg.from, seg.to - seg.from);
            text_elements.emplace_back(std::make_unique<TextElement>(substr, seg.active, seg.from, seg.to));
        }
        return text_elements;
    }

    bool Preprocessor::isCommentAloneOnLine(const std::string &text, size_t commentStart, size_t commentEnd) {
        // Find the start of the line containing the comment
        size_t lineStart = commentStart;
        while (lineStart > 0 && text[lineStart - 1] != '\n')
            lineStart--;

        // Find the end of the line (including newline, if present)
        size_t lineEnd = commentEnd;
        while (lineEnd < text.size() && text[lineEnd] != '\n')
            lineEnd++;
        if (lineEnd < text.size() && text[lineEnd] == '\n')
            lineEnd++; // Include the newline

        // Check if the content between lineStart and commentStart is only whitespace
        bool beforeIsWhitespace = true;
        for (size_t i = lineStart; i < commentStart; ++i) {
            if (text[i] != ' ' && text[i] != '\t') {
                beforeIsWhitespace = false;
                break;
            }
        }

        // Check if the content between commentEnd and lineEnd is only whitespace or newline
        bool afterIsWhitespace = true;
        for (size_t i = commentEnd; i < lineEnd; ++i) {
            if (text[i] != ' ' && text[i] != '\t' && text[i] != '\n') {
                afterIsWhitespace = false;
                break;
            }
        }
        return beforeIsWhitespace && afterIsWhitespace;
    }

    std::vector<std::unique_ptr<parser::elements::TextElement>> Preprocessor::preprocess(
        const std::string &input) {
        auto markerPos = getMarkerPos(input);
        auto segments = findWikiSegments(input, markerPos);
        return toTextElements(input, segments);
    }
}
