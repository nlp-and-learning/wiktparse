#pragma once
#include <string_view>

#include <nlohmann/json.hpp>

// Selection of Wikidata entities needed by wiktparse: languages, parts of speech
// and other Wiktionary section concepts, grammatical features, linguistics terms.
class WikiData {
public:
    // Fast check of raw entity JSON; false means isWiktNeeded() is false too
    static bool mayBeWiktNeeded(std::string_view title, std::string_view text);
    static bool isWiktNeeded(const nlohmann::json &entity);

    static bool isExcluded(const nlohmann::json &entity);
    // Stricter exclusion for entities found only by a label equal to a header
    // term: also works, places, organizations, chemicals ("English" song, "Noun" band)
    static bool isExcludedTermMatch(const nlohmann::json &entity);
    static bool isLanguage(const nlohmann::json &entity);
    // Parts of speech, affixes, phrases, abbreviations, letters...
    static bool isLexical(const nlohmann::json &entity);
    // Grammatical categories and their values (gender, number, case...)
    static bool isGrammatical(const nlohmann::json &entity);
    static bool isLinguistics(const nlohmann::json &entity);
};
