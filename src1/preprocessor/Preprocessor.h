#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H
#include <memory>
#include <vector>

#include "../elements/TextElement.h"

namespace preprocessor {
    using parser::elements::TextElement;
    class Preprocessor {
        using MarkerPos = std::pair<size_t, std::string>;
        struct WikiSegment {
            size_t from = 0;
            size_t to = 0;
            bool active = false;
            WikiSegment(size_t from, size_t to, bool active) : from(from), to(to), active(active) {}
        };
        static size_t len(const std::string& marker);
        static int findMarker(const std::vector<MarkerPos>& pairs, const std::string &marker, int start);
        static std::vector<MarkerPos> getMarkerPos(const std::string& input);
        static std::vector<WikiSegment> findWikiSegments(const std::string& text, const std::vector<MarkerPos>& pairs);
        static std::vector<std::unique_ptr<TextElement>> toTextElements(const std::string& text, const std::vector<WikiSegment>& segments);
        static bool isCommentAloneOnLine(const std::string& text, size_t commentStart, size_t commentEnd);
    public:
        static std::vector<std::unique_ptr<TextElement>> preprocess(const std::string& input);
    };

}

#endif //PREPROCESSOR_H
