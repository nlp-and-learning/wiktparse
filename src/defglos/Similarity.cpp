#include "Similarity.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <wikilib/core/unicode_utils.h>

namespace defglos {

namespace {

// Sub/superscripts as plain characters, typographic spaces, dashes and quotes as ASCII
char32_t plainCodepoint(char32_t c) {
    if (c == 0x00B2) return '2';
    if (c == 0x00B3) return '3';
    if (c == 0x00B9) return '1';
    if (c >= 0x2070 && c <= 0x207F) {
        static constexpr char32_t superscripts[] = {'0', 'i', 0x2072, 0x2073, '4', '5', '6', '7',
                                                    '8', '9', '+', '-', '=', '(', ')', 'n'};
        return superscripts[c - 0x2070];
    }
    if (c >= 0x2080 && c <= 0x208E)
        return U"0123456789+-=()"[c - 0x2080];
    if (c >= 0x2090 && c <= 0x209C)
        return U"aeoxehklmnpst"[c - 0x2090];
    if (c >= 0x2000 && c <= 0x200B) return ' ';
    if (c >= 0x2010 && c <= 0x2015) return '-';
    if (c == 0x2016) return '|';
    if (c == 0x2017) return '_';
    if (c >= 0x2018 && c <= 0x201B) return '\'';
    if (c >= 0x201C && c <= 0x201F) return '"';
    return c;
}

std::string plainText(std::string_view text) {
    std::string result;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t start = pos;
        auto c = wikilib::unicode::decode_utf8(text, pos);
        if (!c) {
            result += text[start];
            pos = start + 1;
            continue;
        }
        result += wikilib::unicode::encode_utf8(plainCodepoint(*c));
    }
    return result;
}

std::string stripPunctuation(const std::string &word) {
    size_t start = 0;
    size_t end = word.size();
    while (start < end && std::ispunct(static_cast<unsigned char>(word[start])))
        ++start;
    while (end > start && std::ispunct(static_cast<unsigned char>(word[end - 1])))
        --end;
    std::string s = word.substr(start, end - start);
    std::erase(s, '-');
    return s;
}

const std::unordered_set<std::string> stopwords = {"of", "a", "an", "or", "the", "to"};

} // namespace

std::vector<std::string> words(std::string_view text, bool withStopwords) {
    std::vector<std::string> result;
    std::string plain = plainText(text);
    size_t pos = 0;
    while (pos < plain.size()) {
        while (pos < plain.size() && std::isspace(static_cast<unsigned char>(plain[pos])))
            ++pos;
        size_t start = pos;
        while (pos < plain.size() && !std::isspace(static_cast<unsigned char>(plain[pos])))
            ++pos;
        auto word = wikilib::unicode::to_lower(stripPunctuation(plain.substr(start, pos - start)));
        if (!word.empty() && (withStopwords || !stopwords.contains(word)))
            result.push_back(std::move(word));
    }
    return result;
}

Similarity::Similarity(const Entry &entry, WordEqual equal) : equal_(std::move(equal)) {
    for (const auto *items : {&entry.defs, &entry.glosses})
        for (const auto &item : *items)
            for (const auto &word : words(item.text, true))
                frequency_[word]++;
    for (const auto &[word, count] : frequency_)
        totalFrequency_ += count;
}

// Weight of a pair of equal words: rare words weigh more
double Similarity::weight(const std::string &a, const std::string &b) const {
    auto itA = frequency_.find(a);
    if (itA == frequency_.end())
        return 0;
    double frequency = itA->second;
    if (a != b) {
        auto itB = frequency_.find(b);
        if (itB != frequency_.end())
            frequency = std::sqrt(itA->second * itB->second);
    }
    return totalFrequency_ / frequency / static_cast<double>(frequency_.size());
}

double Similarity::selfScore(const std::vector<std::string> &words) const {
    if (words.empty())
        return 1;
    std::vector<std::string> unique = words;
    std::ranges::sort(unique);
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    double sum = 0;
    for (const auto &word : unique)
        sum += weight(word, word);
    return sum;
}

// Runs of equal words along diagonals of the word matrix; each run scores the
// square of its summed weights; a run whose words are contained in a longer
// run already counted is skipped
double Similarity::diagonalScore(const std::vector<std::string> &a, const std::vector<std::string> &b) const {
    struct Run {
        std::string words;
        double score = 0;
    };
    auto equal = [&](const std::string &x, const std::string &y) { return x == y || (equal_ && equal_(x, y)); };

    std::map<ptrdiff_t, Run> open; // by diagonal i - j
    std::vector<Run> runs;
    const size_t n = a.size();
    const size_t m = b.size();
    std::vector<std::vector<double>> sum(n + 1, std::vector<double>(m + 1, 0.0));
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            if (!equal(a[i - 1], b[j - 1]))
                continue;
            Run &run = open[static_cast<ptrdiff_t>(i) - static_cast<ptrdiff_t>(j)];
            if (!run.words.empty())
                run.words += ' ';
            run.words += std::min(a[i - 1], b[j - 1]);
            sum[i][j] = sum[i - 1][j - 1] + weight(a[i - 1], b[j - 1]);
            if (i == n || j == m || !equal(a[i], b[j])) {
                run.score = sum[i][j] * sum[i][j];
                auto node = open.extract(static_cast<ptrdiff_t>(i) - static_cast<ptrdiff_t>(j));
                runs.push_back(std::move(node.mapped()));
            }
        }
    }
    std::ranges::stable_sort(runs, std::greater{}, [](const Run &run) { return run.words.size(); });
    std::vector<const Run *> counted;
    double total = 0;
    for (const auto &run : runs) {
        bool contained = std::ranges::any_of(counted, [&](const Run *c) { return c->words.find(run.words) != std::string::npos; });
        if (!contained) {
            counted.push_back(&run);
            total += run.score;
        }
    }
    return total;
}

double Similarity::operator()(std::string_view a, std::string_view b) const {
    auto wordsA = words(a);
    auto wordsB = words(b);
    if (wordsA.empty() || wordsB.empty())
        return 0;
    double norm = std::min(selfScore(wordsA), selfScore(wordsB));
    double normalized = diagonalScore(wordsA, wordsB) / (norm * norm);
    double shorter = static_cast<double>(std::min<size_t>(4, std::min(wordsA.size(), wordsB.size())));
    double longer = static_cast<double>(std::min<size_t>(4, std::max(wordsA.size(), wordsB.size())));
    return normalized * shorter / longer;
}

} // namespace defglos
