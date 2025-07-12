#pragma once
#include <algorithm>
#include <cassert>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "WikiText.h"

class Comments {
    using MarkerPos = std::pair<size_t, std::string>;

    static std::vector<MarkerPos> getMarkerPos(const std::string& input) {
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

    struct WikiSegment {
        size_t from;
        size_t to;
        bool active;
        WikiSegment(size_t from, size_t to, bool active) : from(from), to(to), active(active) {}
    };

    static std::vector<WikiSegment> findWikiSegments(size_t textLen, const std::vector<MarkerPos>& pairs) {
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
                if (closingPos < pairs.size())
                    startSegment = pairs[closingPos].first + len("-->");
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

    static bool isCommentAloneOnLine(const std::string& text, size_t commentStart, size_t commentEnd) {
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

    static std::vector<std::pair<size_t, size_t>> findSegments(const std::string& text, const std::vector<MarkerPos>& pairs) {
        size_t textLen = text.size();
        std::vector<std::pair<size_t, size_t>> segments;
        int startPos = 0;
        size_t startSegment = 0;
        while (startPos < pairs.size()) {
            auto &tag = pairs[startPos];
            if (tag.second == "<nowiki>") {
                auto closingPos = findMarker(pairs, "</nowiki>", startPos + 1);
                if (closingPos < pairs.size()) {
                    if (pairs[startPos].first > startSegment)
                        segments.emplace_back(startSegment, pairs[startPos].first);
                    segments.emplace_back(pairs[startPos].first + len("<nowiki>"), pairs[closingPos].first);
                    startSegment = pairs[closingPos].first + len("</nowiki>");
                    startPos = closingPos + 1;
                }
                else
                    startPos++;
            }
            else if (tag.second == "<!--") {
                assert(pairs[startPos].first >= startSegment);
                if (pairs[startPos].first > startSegment)
                    segments.emplace_back(startSegment, pairs[startPos].first);
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
            segments.emplace_back(startSegment, textLen);
        return segments;
    }

    static size_t len(const std::string& marker) {
        return marker.size();
    }

    static int findMarker(const std::vector<MarkerPos>& pairs, const std::string &marker, int start) {
        for (int i = start; i < pairs.size(); i++) {
            if (pairs[i].second == marker)
                return i;
        }
        return pairs.size();
    }

    static std::string clean(const std::string& text, const std::vector<std::pair<size_t, size_t>> &pairs) {
        std::ostringstream ss;
        for (auto &p : pairs)
            ss << text.substr(p.first, p.second - p.first);
        return ss.str();
    }

    static std::unique_ptr<WikiText> preparse(const std::string& text, const std::vector<WikiSegment>& segments) {
        auto composite = std::make_unique<WikiGroup>();
        for (auto &seg : segments) {
            auto substr = text.substr(seg.from, seg.to - seg.from);
            composite->parts.emplace_back(std::make_unique<WikiFragment>(substr, seg.active));
        }
        if (composite->parts.empty())
            return nullptr;
        else if (composite->parts.size() == 1)
            return std::move(composite->parts[0]);
        else
            return composite;
    }

public:
    static std::string clean(const std::string& text) {
        auto markerPos = getMarkerPos(text);
        auto segments = findSegments(text, markerPos);
        return clean(text, segments);
    }

    static std::unique_ptr<WikiText> preparse(const std::string& text) {
        auto markerPos = getMarkerPos(text);
        auto segments = findWikiSegments(text.size(), markerPos);
        return preparse(text, segments);
    }
};