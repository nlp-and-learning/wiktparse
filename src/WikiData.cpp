#include "WikiData.h"

#include <atomic>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};
using IdSet = std::unordered_set<std::string, StringHash, std::equal_to<>>;

const std::set<std::string> knownGroups = {
    "languageCodeProps", "languageClasses", "languageMetaclasses", "languageItems",
    "lexicalClasses", "lexicalSuperclasses", "lexicalItems",
    "grammaticalClasses", "grammaticalItems",
    "linguisticsClasses", "linguisticsSuperclasses", "linguisticsFields", "linguisticsPartOf", "linguisticsItems",
    "linguisticsDewey",
    "writingSystemClasses",
    "excludedClasses", "excludedItems", "termExcludedClasses", "termExcludedProps", "termAllowedDewey",
};

// Groups of string prefixes instead of ids
const std::set<std::string> prefixGroups = {"linguisticsDewey", "termAllowedDewey"};
constexpr const char *deweyProperty = "P1036";

// Groups whose ids in raw JSON (claim keys, item values) make an entity a candidate
const std::vector<std::string> markerGroups = {
    "languageCodeProps", "languageClasses", "languageMetaclasses", "lexicalClasses", "lexicalSuperclasses",
    "grammaticalClasses", "linguisticsClasses", "linguisticsSuperclasses", "linguisticsFields",
    "linguisticsPartOf", "writingSystemClasses",
};
// Groups of entities selected by their own id
const std::vector<std::string> itemGroups = {"languageItems", "lexicalItems", "grammaticalItems", "linguisticsItems"};

struct Rules {
    std::map<std::string, IdSet> groups;
    IdSet markers;
    IdSet items;

    const IdSet &operator[](const std::string &group) const {
        static const IdSet empty;
        auto it = groups.find(group);
        return it == groups.end() ? empty : it->second;
    }
};

std::shared_ptr<const Rules> loadRulesFile(const fs::path &file) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("Cannot open Wikidata rules: " + file.string());
    auto rules = std::make_shared<Rules>();
    std::string line;
    for (size_t lineNo = 1; std::getline(in, line); lineNo++) {
        if (line.empty() || line[0] == '#')
            continue;
        std::stringstream ss(line);
        std::string group, id;
        std::getline(ss, group, '\t');
        std::getline(ss, id, '\t');
        bool valid = knownGroups.contains(group) &&
                     (prefixGroups.contains(group) ? !id.empty()
                                                   : id.size() >= 2 && (id[0] == 'Q' || id[0] == 'P'));
        if (!valid)
            throw std::runtime_error(file.string() + ":" + std::to_string(lineNo) + ": invalid rule: " + line);
        rules->groups[group].insert(id);
    }
    for (const auto &group : markerGroups)
        rules->markers.insert((*rules)[group].begin(), (*rules)[group].end());
    if (!(*rules)["linguisticsDewey"].empty())
        rules->markers.insert(deweyProperty);
    for (const auto &group : itemGroups)
        rules->items.insert((*rules)[group].begin(), (*rules)[group].end());
    return rules;
}

// Rules are read without locking in hot loops; replaced rules stay alive so that
// references obtained earlier remain valid
std::mutex rulesMutex;
std::vector<std::shared_ptr<const Rules>> allRules;
std::atomic<const Rules *> currentRules{nullptr};

void setRules(std::shared_ptr<const Rules> loaded) {
    std::lock_guard lock(rulesMutex);
    allRules.push_back(loaded);
    currentRules = loaded.get();
}

const Rules &rules() {
    if (const auto *r = currentRules.load())
        return *r;
    std::lock_guard lock(rulesMutex);
    if (!currentRules.load()) {
        allRules.push_back(loadRulesFile(WikiData::defaultRulesPath()));
        currentRules = allRules.back().get();
    }
    return *currentRules.load();
}

std::string entityId(const json &entity) {
    auto id = entity.find("id");
    return id != entity.end() && id->is_string() ? id->get<std::string>() : std::string();
}

// Item ids being values of the given property
std::vector<std::string> claimIds(const json &entity, const char *prop) {
    std::vector<std::string> ids;
    auto claims = entity.find("claims");
    if (claims == entity.end() || !claims->is_object())
        return ids;
    auto statements = claims->find(prop);
    if (statements == claims->end() || !statements->is_array())
        return ids;
    for (const auto &statement : *statements) {
        const auto &value = statement.value("mainsnak", json::object()).value("datavalue", json::object())
                .value("value", json::object());
        if (value.is_object() && value.contains("id") && value["id"].is_string())
            ids.push_back(value["id"].get<std::string>());
    }
    return ids;
}

bool anyClaimIn(const json &entity, const char *prop, const IdSet &set) {
    for (const auto &id : claimIds(entity, prop))
        if (set.contains(id))
            return true;
    return false;
}

// String values of the given property (external ids, classification codes)
std::vector<std::string> claimStrings(const json &entity, const char *prop) {
    std::vector<std::string> values;
    auto claims = entity.find("claims");
    if (claims == entity.end() || !claims->is_object())
        return values;
    auto statements = claims->find(prop);
    if (statements == claims->end() || !statements->is_array())
        return values;
    for (const auto &statement : *statements) {
        const auto &value = statement.value("mainsnak", json::object()).value("datavalue", json::object())
                .value("value", json());
        if (value.is_string())
            values.push_back(value.get<std::string>());
    }
    return values;
}

bool anyClaimWithPrefix(const json &entity, const char *prop, const IdSet &prefixes) {
    for (const auto &value : claimStrings(entity, prop))
        for (const auto &prefix : prefixes)
            if (value.starts_with(prefix))
                return true;
    return false;
}

bool hasClaim(const json &entity, const std::string &prop) {
    auto claims = entity.find("claims");
    return claims != entity.end() && claims->is_object() && claims->contains(prop);
}

bool hasAnyClaim(const json &entity, const IdSet &props) {
    for (const auto &prop : props)
        if (hasClaim(entity, prop))
            return true;
    return false;
}

bool isItem(const json &entity) {
    return entity.value("type", "") == "item" && entity.contains("id");
}

} // namespace

void WikiData::loadRules(const fs::path &file) {
    setRules(loadRulesFile(file));
}

fs::path WikiData::defaultRulesPath() {
    return fs::path(WIKTPARSE_RES_DIR) / "wikidata_selection.tsv";
}

bool WikiData::mayBeWiktNeeded(std::string_view title, std::string_view text) {
    const auto &r = rules();
    if (r.items.contains(title))
        return true;
    // Scan quoted tokens like "Q34770" or "P220" (claim keys and item values)
    for (size_t pos = text.find('"'); pos != std::string_view::npos; pos = text.find('"', pos + 1)) {
        if (pos + 2 >= text.size() || (text[pos + 1] != 'Q' && text[pos + 1] != 'P'))
            continue;
        size_t end = pos + 2;
        while (end < text.size() && text[end] >= '0' && text[end] <= '9')
            end++;
        if (end == pos + 2 || end >= text.size() || text[end] != '"')
            continue;
        if (r.markers.contains(text.substr(pos + 1, end - pos - 1)))
            return true;
        pos = end;
    }
    return false;
}

bool WikiData::isExcluded(const json &entity) {
    const auto &r = rules();
    return !isItem(entity) || anyClaimIn(entity, "P31", r["excludedClasses"]) ||
           r["excludedItems"].contains(entityId(entity));
}

bool WikiData::isExcludedTermMatch(const json &entity) {
    const auto &r = rules();
    if (isExcluded(entity) || anyClaimIn(entity, "P31", r["termExcludedClasses"]) ||
        hasAnyClaim(entity, r["termExcludedProps"]))
        return true;
    // Classified by Dewey only outside fields related to dictionaries
    const auto &allowed = r["termAllowedDewey"];
    if (allowed.empty())
        return false;
    bool classified = false;
    for (const auto &code : claimStrings(entity, deweyProperty)) {
        if (code.empty() || code[0] < '0' || code[0] > '9')
            continue;
        classified = true;
        for (const auto &prefix : allowed)
            if (code.starts_with(prefix))
                return false;
    }
    return classified;
}

bool WikiData::isLanguage(const json &entity) {
    const auto &r = rules();
    return hasAnyClaim(entity, r["languageCodeProps"]) || anyClaimIn(entity, "P31", r["languageClasses"]) ||
           anyClaimIn(entity, "P31", r["languageMetaclasses"]) || anyClaimIn(entity, "P279", r["languageClasses"]) ||
           r["languageItems"].contains(entityId(entity));
}

bool WikiData::isLexical(const json &entity) {
    const auto &r = rules();
    if (anyClaimIn(entity, "P31", r["lexicalClasses"]))
        return true;
    for (const auto &id : claimIds(entity, "P279"))
        if (r["lexicalClasses"].contains(id) || r["lexicalSuperclasses"].contains(id))
            return true;
    return r["lexicalItems"].contains(entityId(entity));
}

bool WikiData::isGrammatical(const json &entity) {
    const auto &r = rules();
    return anyClaimIn(entity, "P31", r["grammaticalClasses"]) || r["grammaticalItems"].contains(entityId(entity));
}

bool WikiData::isLinguistics(const json &entity) {
    const auto &r = rules();
    return anyClaimIn(entity, "P279", r["linguisticsClasses"]) || anyClaimIn(entity, "P31", r["linguisticsClasses"]) ||
           anyClaimIn(entity, "P279", r["linguisticsSuperclasses"]) ||
           anyClaimIn(entity, "P2579", r["linguisticsFields"]) || anyClaimIn(entity, "P361", r["linguisticsPartOf"]) ||
           anyClaimWithPrefix(entity, deweyProperty, r["linguisticsDewey"]) ||
           r["linguisticsItems"].contains(entityId(entity));
}

bool WikiData::isWritingSystem(const json &entity) {
    const auto &r = rules();
    return anyClaimIn(entity, "P31", r["writingSystemClasses"]) ||
           anyClaimIn(entity, "P279", r["writingSystemClasses"]);
}

bool WikiData::isWiktNeeded(const json &entity) {
    if (isExcluded(entity))
        return false;
    return isLanguage(entity) || isLexical(entity) || isGrammatical(entity) || isLinguistics(entity) ||
           isWritingSystem(entity);
}
