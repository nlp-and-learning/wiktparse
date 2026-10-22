// Links English definitions to translation glosses on the defglos samples and
// measures the result against the expected links (precision, recall).
//
// defglos [samples.txt] [-v] [--sweep]
//   samples.txt  default res/defglos/samples.txt
//   -v           list wrong and missing links of every sample
//   --sweep      precision and recall for a grid of thresholds
#include <algorithm>
#include <exception>
#include <format>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "defglos/Matcher.h"
#include "defglos/Samples.h"

using namespace defglos;

namespace {

struct Score {
    size_t found = 0;    // links found
    size_t correct = 0;  // found and expected
    size_t expected = 0; // expected links
    size_t hard = 0;     // expected hard links
    size_t hardFound = 0;

    void add(const Score &other) {
        found += other.found;
        correct += other.correct;
        expected += other.expected;
        hard += other.hard;
        hardFound += other.hardFound;
    }

    [[nodiscard]] double precision() const { return found ? double(correct) / double(found) : 1; }
    [[nodiscard]] double recall() const { return expected ? double(correct) / double(expected) : 1; }
};

Score evaluate(const Sample &sample, const MatchOptions &options, bool verbose) {
    const auto &entry = sample.entry;
    Similarity similarity(entry);
    auto links = match(entry, similarity, options);

    std::set<std::pair<size_t, size_t>> expected;
    std::set<std::pair<size_t, size_t>> hard;
    for (const auto &link : sample.expected) {
        expected.emplace(link.def, link.gloss);
        if (link.hard)
            hard.emplace(link.def, link.gloss);
    }
    std::set<std::pair<size_t, size_t>> found;
    Score score;
    for (const auto &link : links) {
        found.emplace(link.def, link.gloss);
        bool ok = expected.contains({link.def, link.gloss});
        score.correct += ok;
        score.hardFound += hard.contains({link.def, link.gloss});
        if (verbose && !ok)
            std::cout << std::format("  wrong   {} - {}  {:.2f} {}\n    {}\n    {}\n", entry.defs[link.def].id,
                                     entry.glosses[link.gloss].id, link.score, methodName(link.method),
                                     entry.defs[link.def].text, entry.glosses[link.gloss].text);
    }
    score.found = links.size();
    score.expected = expected.size();
    score.hard = hard.size();
    if (verbose)
        for (const auto &[def, gloss] : expected)
            if (!found.contains({def, gloss}))
                std::cout << std::format("  missing {} - {}  {:.2f}{}\n    {}\n    {}\n", entry.defs[def].id,
                                         entry.glosses[gloss].id, similarity(entry.defs[def].text, entry.glosses[gloss].text),
                                         hard.contains({def, gloss}) ? " hard" : "", entry.defs[def].text,
                                         entry.glosses[gloss].text);
    return score;
}

Score evaluateAll(const std::vector<Sample> &samples, const MatchOptions &options, bool verbose) {
    Score total;
    for (const auto &sample : samples) {
        if (verbose)
            std::cout << std::format("= {} - {} - {}\n", sample.entry.title, sample.entry.etymology, sample.entry.pos);
        auto score = evaluate(sample, options, verbose);
        if (verbose)
            std::cout << std::format("  precision {:.2f} recall {:.2f}\n", score.precision(), score.recall());
        total.add(score);
    }
    return total;
}

void printTotal(const Score &total) {
    std::cout << std::format("links {} found, {} correct, {} expected ({} hard, {} of them found)\n", total.found,
                             total.correct, total.expected, total.hard, total.hardFound);
    std::cout << std::format("precision {:.3f} recall {:.3f}\n", total.precision(), total.recall());
}

} // namespace

int main(int argc, char *argv[]) {
    try {
        std::string path = WIKTPARSE_RES_DIR "/defglos/samples.txt";
        bool verbose = false;
        bool sweep = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-v")
                verbose = true;
            else if (arg == "--sweep")
                sweep = true;
            else
                path = arg;
        }
        auto samples = readSamples(path);
        std::cout << samples.size() << " samples\n";
        if (sweep) {
            std::cout << "positional best hierarchy precision recall\n";
            for (double positional : {0.02, 0.05, 0.1, 0.2, 0.3, 0.5})
                for (double best : {0.1, 0.2, 0.3, 0.5, 0.8})
                    for (bool hierarchy : {false, true}) {
                        MatchOptions options{positional, best, hierarchy};
                        auto total = evaluateAll(samples, options, false);
                        std::cout << std::format("{:.2f} {:.2f} {} {:.3f} {:.3f}\n", positional, best, hierarchy,
                                                 total.precision(), total.recall());
                    }
            return 0;
        }
        printTotal(evaluateAll(samples, {}, verbose));
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
