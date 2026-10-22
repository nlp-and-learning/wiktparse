#include <gtest/gtest.h>
#include <set>
#include <sstream>
#include "defglos/Matcher.h"
#include "defglos/Samples.h"

using namespace defglos;

TEST(DefglosTest, ReadSample) {
    std::istringstream in("= band - Etym 1 - verb\n"
                          "1. (transitive) To fasten with a band.\n"
                          "2. (transitive, ornithology) To fasten an identifying band around the leg of (a bird).\n"
                          "#\n"
                          "a. fasten with a band\n"
                          "b. ornithology: fasten an identifying band around the leg of\n"
                          "#\n"
                          "1 - a\n"
                          "2 - b - hard\n"
                          "1.? - b\n"
                          "\n");
    auto samples = readSamples(in);
    ASSERT_EQ(samples.size(), 1u);
    const auto &sample = samples[0];
    EXPECT_EQ(sample.entry.title, "band");
    EXPECT_EQ(sample.entry.etymology, "Etym 1");
    EXPECT_EQ(sample.entry.pos, "verb");
    ASSERT_EQ(sample.entry.defs.size(), 2u);
    EXPECT_EQ(sample.entry.defs[1].id, "2");
    EXPECT_EQ(sample.entry.glosses[0].text, "fasten with a band");
    ASSERT_EQ(sample.expected.size(), 2u); // unknown "1.?" skipped
    EXPECT_FALSE(sample.expected[0].hard);
    EXPECT_TRUE(sample.expected[1].hard);
    EXPECT_EQ(sample.expected[1].def, 1u);
    EXPECT_EQ(sample.expected[1].gloss, 1u);
}

TEST(DefglosTest, Words) {
    EXPECT_EQ(words("(physics) A part of the H₂O—spectrum."),
              (std::vector<std::string>{"physics", "part", "h2ospectrum"}));
    EXPECT_EQ(words("Of the", true), (std::vector<std::string>{"of", "the"}));
}

TEST(DefglosTest, Subdefinition) {
    EXPECT_TRUE(isSubdefinition("3.1", "3"));
    EXPECT_TRUE(isSubdefinition("3.1.2", "3"));
    EXPECT_FALSE(isSubdefinition("31", "3"));
    EXPECT_FALSE(isSubdefinition("3", "3"));
}

TEST(DefglosTest, MatchPositionalAndBest) {
    Entry entry;
    entry.defs = {{"1", "A domesticated feline animal kept as a house pet."},
                  {"2", "(slang) A jazz musician."},
                  {"3", "A whip with nine knotted cords."}};
    entry.glosses = {{"a", "whip"}, {"b", "domesticated feline"}, {"c", "jazz musician"}};
    Similarity similarity(entry);
    auto links = match(entry, similarity);
    std::set<std::pair<size_t, size_t>> found;
    for (const auto &link : links)
        found.emplace(link.def, link.gloss);
    EXPECT_EQ(found, (std::set<std::pair<size_t, size_t>>{{0, 1}, {1, 2}, {2, 0}}));
}

// Regression on the samples; numbers from defglos with default options
TEST(DefglosTest, SamplesQuality) {
    auto samples = readSamples(WIKTPARSE_RES_DIR "/defglos/samples.txt");
    ASSERT_EQ(samples.size(), 20u);
    size_t found = 0, correct = 0, expected = 0;
    for (const auto &sample : samples) {
        std::set<std::pair<size_t, size_t>> wanted;
        for (const auto &link : sample.expected)
            wanted.emplace(link.def, link.gloss);
        Similarity similarity(sample.entry);
        for (const auto &link : match(sample.entry, similarity)) {
            ++found;
            correct += wanted.contains({link.def, link.gloss});
        }
        expected += wanted.size();
    }
    EXPECT_GE(double(correct) / double(found), 0.99);
    EXPECT_GE(double(correct) / double(expected), 0.89);
}
