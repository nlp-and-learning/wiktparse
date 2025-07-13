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

    std::vector<Preprocessor::WikiSegment> Preprocessor::findWikiSegments(size_t textLen,
        const std::vector<MarkerPos> &pairs) {
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

    std::vector<std::unique_ptr<parser::elements::TextElement>> Preprocessor::preprocess(
        const std::string &input) {
        auto markerPos = getMarkerPos(input);
        auto segments = findWikiSegments(input.size(), markerPos);
        return toTextElements(input, segments);
    }
}
