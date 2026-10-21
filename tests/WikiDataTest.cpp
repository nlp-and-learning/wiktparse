#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "WikiData.h"

using json = nlohmann::json;

namespace {
// Item JSON in Wikidata dump format with item-valued claims
std::string item(const std::string &id, const std::vector<std::pair<std::string, std::string>> &claims) {
    json entity = {{"type", "item"}, {"id", id}, {"labels", {{"en", {{"language", "en"}, {"value", id}}}}}};
    json jsonClaims = json::object();
    for (const auto &[prop, value] : claims) {
        json snak = {{"snaktype", "value"}, {"property", prop}};
        if (!value.empty())
            snak["datavalue"] = {{"value", {{"entity-type", "item"}, {"id", value}}}, {"type", "wikibase-entityid"}};
        jsonClaims[prop].push_back({{"mainsnak", snak}, {"type", "statement"}});
    }
    entity["claims"] = jsonClaims;
    return entity.dump();
}

bool needed(const std::string &text) {
    auto entity = json::parse(text);
    bool result = WikiData::isWiktNeeded(entity);
    // The fast check must never reject a needed entity
    if (result)
        EXPECT_TRUE(WikiData::mayBeWiktNeeded(entity["id"].get<std::string>(), text)) << text;
    return result;
}
} // namespace

TEST(WikiDataTest, LanguagesByCodeOrClass) {
    EXPECT_TRUE(needed(item("Q809", {{"P220", ""}, {"P31", "Q1288568"}})));
    EXPECT_TRUE(needed(item("Q100", {{"P424", ""}})));
    EXPECT_TRUE(needed(item("Q101", {{"P31", "Q33384"}})));  // dialect
    EXPECT_TRUE(needed(item("Q102", {{"P31", "Q38058796"}}))); // extinct language
    EXPECT_TRUE(WikiData::isLanguage(json::parse(item("Q103", {{"P31", "Q17376908"}}))));
}

TEST(WikiDataTest, PartsOfSpeechAndSectionConcepts) {
    EXPECT_TRUE(needed(item("Q1084", {{"P31", "Q82042"}})));    // noun
    EXPECT_TRUE(needed(item("Q147276", {{"P279", "Q1084"}})));  // proper noun
    EXPECT_TRUE(needed(item("Q134830", {{"P279", "Q62155"}}))); // prefix
    EXPECT_TRUE(needed(item("Q184511", {{"P31", "Q135929641"}, {"P279", "Q187931"}}))); // idiom
    EXPECT_TRUE(needed(item("Q35102", {{"P31", "Q223393"}, {"P279", "Q187931"}})));     // proverb
    EXPECT_TRUE(needed(item("Q918270", {{"P279", "Q102786"}}))); // initialism
    EXPECT_TRUE(needed(item("Q80071", {{"P279", "Q151885"}})));  // symbol, listed explicitly
    EXPECT_TRUE(needed(item("Q35245", {})));                     // etymology, listed explicitly
    auto properNoun = json::parse(item("Q147276", {{"P279", "Q1084"}}));
    EXPECT_TRUE(WikiData::isLexical(properNoun));
    EXPECT_FALSE(WikiData::isLanguage(properNoun));
}

TEST(WikiDataTest, GrammaticalFeatures) {
    EXPECT_TRUE(needed(item("Q499327", {{"P31", "Q162378"}}))); // masculine
    EXPECT_TRUE(needed(item("Q131105", {{"P31", "Q128234"}}))); // nominative
    EXPECT_TRUE(WikiData::isGrammatical(json::parse(item("Q110786", {{"P31", "Q104083"}}))));
}

TEST(WikiDataTest, LanguageTypes) {
    EXPECT_TRUE(needed(item("Q335214", {{"P279", "Q34770"}})));   // endangered language
    EXPECT_TRUE(needed(item("Q23492", {{"P31", "Q20829075"}})));  // official language
    EXPECT_TRUE(needed(item("Q33384", {{"P31", "Q28923954"}})));  // dialect
    auto language = json::parse(item("Q34770", {{"P31", "Q28923954"}}));
    EXPECT_TRUE(WikiData::isLanguage(language));
    EXPECT_FALSE(WikiData::isLexical(language));
    EXPECT_TRUE(needed(item("Q34770", {})));
}

TEST(WikiDataTest, LinguisticsTerms) {
    EXPECT_TRUE(needed(item("Q200", {{"P279", "Q8162"}})));
    EXPECT_TRUE(needed(item("Q201", {{"P361", "Q34770"}})));
    EXPECT_TRUE(needed(item("Q146863", {{"P279", "Q8183"}})));    // allophone
    EXPECT_TRUE(needed(item("Q202", {{"P2579", "Q40998"}})));     // studied by phonology
    EXPECT_TRUE(needed(item("Q203", {{"P31", "Q66664364"}})));    // linguistic term
    EXPECT_TRUE(needed(item("Q8162", {})));
    EXPECT_FALSE(WikiData::isLexical(json::parse(item("Q8162", {}))));
}

TEST(WikiDataTest, JunkIsSkipped) {
    // Scholarly article about linguistics, human, category, family name
    EXPECT_FALSE(needed(item("Q300", {{"P31", "Q13442814"}, {"P921", "Q8162"}, {"P407", "Q34770"}})));
    EXPECT_FALSE(needed(item("Q301", {{"P31", "Q5"}, {"P279", "Q8162"}})));
    EXPECT_FALSE(needed(item("Q302", {{"P31", "Q4167836"}, {"P301", "Q1084"}})));
    EXPECT_FALSE(needed(item("Q303", {{"P31", "Q101352"}, {"P407", "Q809"}})));
    EXPECT_FALSE(needed(item("Q304", {{"P31", "Q1298668"}, {"P279", "Q8162"}})));
    // Mentions a language only as a value of another property
    EXPECT_FALSE(needed(item("Q305", {{"P31", "Q515"}, {"P2936", "Q34770"}})));
    // A concrete word is not a lexical class
    EXPECT_FALSE(needed(item("Q306", {{"P31", "Q8171"}})));
}

TEST(WikiDataTest, WritingSystems) {
    EXPECT_TRUE(needed(item("Q8209", {{"P31", "Q335806"}})));   // Devanagari: abugida
    EXPECT_TRUE(needed(item("Q401", {{"P31", "Q4004706"}})));   // unicase alphabet
    EXPECT_TRUE(WikiData::isWritingSystem(json::parse(item("Q9779", {{"P279", "Q8192"}}))));
    EXPECT_FALSE(WikiData::isWritingSystem(json::parse(item("Q402", {{"P31", "Q19776628"}})))); // letter
}

TEST(WikiDataTest, TermMatchesOfPlacesAreExcluded) {
    auto place = json::parse(item("Q500", {{"P31", "Q123456"}, {"P625", ""}}));
    EXPECT_TRUE(WikiData::isExcludedTermMatch(place));
    EXPECT_FALSE(WikiData::isExcluded(place));
    EXPECT_TRUE(WikiData::isExcludedTermMatch(json::parse(item("Q501", {{"P131", "Q1"}}))));
    EXPECT_TRUE(WikiData::isExcludedTermMatch(json::parse(item("Q502", {{"P31", "Q41710"}})))); // ethnic group
    EXPECT_FALSE(WikiData::isExcludedTermMatch(json::parse(item("Q503", {{"P31", "Q151885"}})))); // concept
}

TEST(WikiDataTest, DeweyLinguisticsAndExcludedItems) {
    auto entity = json::parse(item("Q600", {{"P31", "Q151885"}}));
    entity["claims"]["P1036"] = json::array({{{"mainsnak", {{"snaktype", "value"}, {"property", "P1036"},
                                                           {"datavalue", {{"value", "415.62"}, {"type", "string"}}}}}}});
    EXPECT_TRUE(WikiData::isLinguistics(entity));
    EXPECT_TRUE(WikiData::mayBeWiktNeeded("Q600", entity.dump()));
    entity["claims"]["P1036"][0]["mainsnak"]["datavalue"]["value"] = "425.1"; // English grammar: a language
    EXPECT_FALSE(WikiData::isLinguistics(entity));

    EXPECT_TRUE(WikiData::isExcluded(json::parse(item("Q202444", {{"P31", "Q121493639"}})))); // given name
    EXPECT_TRUE(WikiData::isExcludedTermMatch(json::parse(item("Q801", {{"P31", "Q21199"}})))); // number
}

TEST(WikiDataTest, RulesFileErrors) {
    EXPECT_THROW(WikiData::loadRules("/nonexistent/rules.tsv"), std::runtime_error);
    auto bad = std::filesystem::temp_directory_path() / "wiktparse-bad-rules.tsv";
    std::ofstream(bad) << "noSuchGroup\tQ1\tcomment\n";
    EXPECT_THROW(WikiData::loadRules(bad), std::runtime_error);
    std::filesystem::remove(bad);
    // Failed loads keep the previous rules
    EXPECT_TRUE(WikiData::isLanguage(json::parse(item("Q809", {{"P220", ""}}))));
}

TEST(WikiDataTest, RedirectsAndMissingValues) {
    EXPECT_FALSE(WikiData::isWiktNeeded(json::parse(R"({"entity":"Q1","redirect":"Q2"})")));
    EXPECT_FALSE(WikiData::isWiktNeeded(json::parse(R"({"type":"item","id":"Q1","claims":[]})")));
    EXPECT_FALSE(needed(item("Q400", {{"P31", ""}})));
}

TEST(WikiDataTest, FastCheck) {
    EXPECT_TRUE(WikiData::mayBeWiktNeeded("Q1", R"({"claims":{"P220":[]}})"));
    EXPECT_TRUE(WikiData::mayBeWiktNeeded("Q1", R"({"id":"Q34770"})"));
    EXPECT_TRUE(WikiData::mayBeWiktNeeded("Q80071", "{}"));
    EXPECT_FALSE(WikiData::mayBeWiktNeeded("Q1", R"({"id":"Q347700","x":"Q3477","P2200":1})"));
    EXPECT_FALSE(WikiData::mayBeWiktNeeded("Q1", R"({"claims":{"P31":[{"id":"Q515"}]}} "Q)"));
}
