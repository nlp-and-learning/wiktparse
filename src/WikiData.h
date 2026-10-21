#pragma once
#include <filesystem>
#include <string_view>

#include <nlohmann/json.hpp>

// Selection of Wikidata entities needed by wiktparse: languages, parts of speech
// and other Wiktionary section concepts, grammatical features, linguistics terms,
// writing systems. Ids of classes and properties are read from a rules file
// (res/wikidata_selection.tsv), loaded on first use unless loadRules() was called.
class WikiData {
public:
    // Replaces the rules; call before using WikiData from several threads
    static void loadRules(const std::filesystem::path &file);
    static std::filesystem::path defaultRulesPath();

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
    static bool isWritingSystem(const nlohmann::json &entity);
};
