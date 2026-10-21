#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// Terms of Wiktionary headers (wiktterms output: <dir>/<wiki>.txt) by label
// language, to find Wikidata entities whose label or alias is such a term
// in the language of that Wiktionary. Comparison is case-insensitive.
class WikiTerms {
    std::unordered_map<std::string, std::unordered_set<std::string>> terms; // label language -> lowercase terms
    size_t maxBytes = 0;
    bool contains(std::string_view language, std::string_view value) const;
public:
    // Returns number of loaded terms
    size_t load(const std::filesystem::path &dir);
    [[nodiscard]] bool empty() const { return terms.empty(); }
    // Wikidata label language of a Wiktionary: simple -> en, zh_min_nan -> nan
    static std::string labelLanguage(const std::string &wiki);
    // Fast check of raw entity JSON: some {"language":..,"value":..} is a term
    // (labels, aliases and descriptions); false means matches() is empty
    [[nodiscard]] bool mayMatch(std::string_view text) const;
    // (language, label or alias) of the entity which are terms
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> matches(const nlohmann::json &entity) const;
    // Like matches(), with terms in the compared (lowercase) form
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> matchedTerms(const nlohmann::json &entity) const;
    // Decodes a JSON string body (without quotes): \uXXXX, \", \\ ...
    static std::string unescapeJson(std::string_view raw);
};
