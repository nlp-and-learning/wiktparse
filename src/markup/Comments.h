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
        WikiSegment(size_t from, size_t to, bool active):from(from), to(to), active(active) {}
    };
    static std::vector<WikiSegment> findWikiSegments(size_t textLen, const std::vector<MarkerPos>& pairs) {
        std::vector<WikiSegment> segments;
        int startPos = 0;
        size_t startSegment = 0;
        while (startPos < pairs.size()) {
            auto &tag = pairs[startPos];
            if (tag.second == "<nowiki>" ) {
                auto closingPos = findMarker(pairs, "</nowiki>", startPos + 1);
                if (closingPos<pairs.size()) {
                    if (pairs[startPos].first > startSegment)
                        segments.emplace_back(startSegment, pairs[startPos].first, true);
                    segments.emplace_back(pairs[startPos].first+len("<nowiki>"), pairs[closingPos].first, false);
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


    static std::vector<std::pair<size_t, size_t>> findSegments(size_t textLen, const std::vector<MarkerPos>& pairs) {
        std::vector<std::pair<size_t, size_t>> segments;
        int startPos = 0;
        size_t startSegment = 0;
        while (startPos < pairs.size()) {
            auto &tag = pairs[startPos];
            if (tag.second == "<nowiki>" ) {
                auto closingPos = findMarker(pairs, "</nowiki>", startPos + 1);
                if (closingPos<pairs.size()) {
                    if (pairs[startPos].first > startSegment)
                        segments.emplace_back(startSegment, pairs[startPos].first);
                    segments.emplace_back(pairs[startPos].first+len("<nowiki>"), pairs[closingPos].first);
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
            segments.emplace_back(startSegment, textLen);
        return segments;
    }
    static size_t len(const std::string& marker) {
        return marker.size();
    }

    static int findMarker(const std::vector<MarkerPos>& pairs, const std::string &marker, int start) {
        for (int i = start; i<pairs.size(); i++) {
            if (pairs[i].second == marker)
                return i;
        }
        return pairs.size();
    }
    static std::string clean(const std::string& text, const std::vector<std::pair<size_t, size_t>> & pairs) {
        std::ostringstream ss;
        for (auto &p: pairs)
            ss << text.substr(p.first, p.second - p.first);
        return ss.str();
    }

    static std::unique_ptr<WikiText> preparse(const std::string& text, const std::vector<WikiSegment>& segments) {
        auto composite = std::make_unique<WikiGroup>();
        for (auto &seg: segments) {
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
        auto segments = findSegments(text.size(),markerPos);
        return clean(text, segments);
    }

    static std::unique_ptr<WikiText> preparse(const std::string& text) {
        auto markerPos = getMarkerPos(text);
        auto segments = findWikiSegments(text.size(),markerPos);
        return preparse(text, segments);
    }
};
