#pragma once
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "Samples.h"

namespace defglos {

// Words of a definition or gloss: lowercase, sub/superscripts and typographic
// dashes, quotes and spaces as ASCII, punctuation stripped from both ends,
// hyphens removed; without stopwords (of, a, an, or, the, to) unless asked
std::vector<std::string> words(std::string_view text, bool withStopwords = false);

// Whether two words count as equal (e.g. FastText cosine >= 0.5); default: identical
using WordEqual = std::function<bool(const std::string &, const std::string &)>;

// Similarity of a definition and a gloss: common runs of words weighted by
// rarity in the entry (rare words count more), normalized by the self-score
// of the shorter text and scaled down when lengths differ (up to 4 words)
class Similarity {
public:
    explicit Similarity(const Entry &entry, WordEqual equal = {});

    [[nodiscard]] double operator()(std::string_view a, std::string_view b) const;

private:
    std::map<std::string, int> frequency_;
    int totalFrequency_ = 0;
    WordEqual equal_;

    [[nodiscard]] double weight(const std::string &a, const std::string &b) const;
    [[nodiscard]] double selfScore(const std::vector<std::string> &words) const;
    [[nodiscard]] double diagonalScore(const std::vector<std::string> &a, const std::vector<std::string> &b) const;
};

} // namespace defglos
