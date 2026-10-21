#include "WikiData.h"

#include <string>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;
using IdSet = std::unordered_set<std::string_view>;

namespace {

// Properties holding language codes
const IdSet languageCodeProps = {
    "P218",  // ISO 639-1
    "P219",  // ISO 639-2
    "P220",  // ISO 639-3
    "P305",  // IETF language tag
    "P424",  // Wikimedia language code
    "P1394", // Glottolog
    "P1232", // Linguist List
    "P1627", // Ethnologue
    "P278",  // GOST 7.75-97
    "P2355", // UNESCO Atlas of the World's Languages in Danger
    "P2192", // Endangered Languages Project
    "P2590", // BPS language code (Indonesia)
    "P2589", // Indonesian ethnic group
};

// P31 of language types like official, synthetic, minority language, dialect
const IdSet languageMetaclasses = {
    "Q28923954", // languoid class
    "Q20829075", // type of language
};

// P31 of languages; P279 of language types
const IdSet languageClasses = {
    "Q34770",    // language
    "Q1288568",  // modern language
    "Q33742",    // natural language
    "Q20162172", // human language
    "Q38058796", // extinct language
    "Q45762",    // dead language
    "Q436240",   // ancient language
    "Q2315359",  // historical language
    "Q4085712",  // unwritten language
    "Q1149626",  // written language
    "Q33384",    // dialect
    "Q34228",    // sign language
    "Q33215",    // constructed language
    "Q33289",    // creole
    "Q33831",    // pidgin
    "Q152559",   // macrolanguage
    "Q25295",    // language family
    "Q17376908", // languoid
    "Q3329375",  // language variety
    "Q12442971", // lect
};

// P31 or P279 of parts of speech and similar lexical classes
const IdSet lexicalClasses = {
    "Q82042",    // part of speech
    "Q1084",     // noun
    "Q24905",    // verb
    "Q34698",    // adjective
    "Q380057",   // adverb
    "Q36224",    // pronoun
    "Q63116",    // numeral
    "Q103184",   // article
    "Q576271",   // determiner
    "Q36484",    // conjunction
    "Q83034",    // interjection
    "Q4833830",  // preposition
    "Q161873",   // postposition
    "Q134316",   // adposition
    "Q184943",   // grammatical particle
    "Q2120608",  // function word
    "Q789016",   // content word
    "Q503992",   // nominal
    "Q2006180",  // pro-form
    "Q62155",    // affix
    "Q111029",   // root
    "Q184511",   // idiom
    "Q35102",    // proverb
    "Q1778442",  // verb phrase
    "Q101244",   // acronym
    "Q918270",   // initialism
    "Q126473",   // contraction
    "Q54553409", // verb form
    "Q207857",   // inflection
};

// P279 only: instances of these are concrete words, letters, phrases
const IdSet lexicalSuperclasses = {
    "Q8171",     // word
    "Q43249",    // morpheme
    "Q187931",   // phrase
    "Q48937186", // word or phrase
    "Q102786",   // abbreviation
    "Q111352",   // lexeme
    "Q2545446",  // grapheme
    "Q11953984", // linguistic unit
    "Q115786086",// form
};

// P31 of grammatical categories and their values
const IdSet grammaticalClasses = {
    "Q980357", // grammatical category
    "Q162378", // grammatical gender
    "Q104083", // grammatical number
    "Q128234", // case
    "Q184932", // grammatical mood
};

// P31, P279 or P361 (part of) of linguistics terms
const IdSet linguisticsClasses = {
    "Q8162",     // linguistics
    "Q1759988",  // linguistic norm
    "Q66664364", // linguistic term
};

// P279 only
const IdSet linguisticsSuperclasses = {
    "Q8183", // phoneme
};

// P2579 (studied by) of linguistics terms
const IdSet linguisticsFields = {
    "Q8162",   // linguistics
    "Q160845", // sociolinguistics
    "Q40998",  // phonology
    "Q35395",  // phonetics
    "Q192625", // linguistic typology
    "Q190375", // historical linguistics
    "Q35245",  // etymology
};

// Wiktionary section concepts not reachable by the rules above
const IdSet explicitItems = {
    "Q80071",   // symbol
    "Q3241972", // character
    "Q82622",   // punctuation
    "Q162940",  // diacritic
    "Q9788",    // letter
    "Q184377",  // pronunciation
    "Q35245",   // etymology
    "Q976327",  // romanization
    "Q1122269", // collocation
    "Q170239",  // onomatopoeia
    "Q8171",    // word
    "Q43249",   // morpheme
    "Q187931",  // phrase
    "Q102786",  // abbreviation
    "Q111352",  // lexeme
    "Q2545446", // grapheme
    "Q82042",   // part of speech
};

// Top concepts of the other groups, which have no matching P31/P279 themselves
constexpr std::string_view languageItem = "Q34770";        // language
constexpr std::string_view grammaticalItem = "Q980357";    // grammatical category
constexpr std::string_view linguisticsItem = "Q8162";      // linguistics

std::string entityId(const json &entity) {
    auto id = entity.find("id");
    return id != entity.end() && id->is_string() ? id->get<std::string>() : std::string();
}

// P31 of entities never needed even if they mention languages
const IdSet excludedClasses = {
    "Q5",         // human
    "Q15632617",  // fictional human
    "Q101352",    // family name
    "Q202444",    // given name
    "Q13442814",  // scholarly article
    "Q5633421",   // scientific journal
    "Q187685",    // doctoral thesis
    "Q1907875",   // master's thesis
    "Q3331189",   // version, edition or translation
    "Q7725634",   // literary work
    "Q47461344",  // written work
    "Q732577",    // publication
    "Q571",       // book
    "Q11424",     // film
    "Q1259759",   // miniseries
    "Q16521",     // taxon
    "Q1298668",   // science project
    "Q2300983",   // Collaborative Research Centre
    "Q4167836",   // Wikimedia category
    "Q4167410",   // Wikimedia disambiguation page
    "Q11266439",  // Wikimedia template
    "Q19887878",  // Wikimedia infobox template
    "Q17442446",  // Wikimedia internal item
};

// P31 of entities never needed when found only by a label (header term)
const IdSet termExcludedClasses = {
    "Q7366",      // song
    "Q482994",    // album
    "Q134556",    // single
    "Q105543609", // musical work/composition
    "Q1344",      // opera
    "Q215380",    // musical group
    "Q5398426",   // television series
    "Q15416",     // television program
    "Q21191270",  // television series episode
    "Q24862",     // short film
    "Q7889",      // video game
    "Q3305213",   // painting
    "Q838948",    // work of art
    "Q17537576",  // creative work
    "Q41298",     // magazine
    "Q1002697",   // periodical
    "Q11032",     // newspaper
    "Q4830453",   // business
    "Q783794",    // company
    "Q891723",    // public company
    "Q43229",     // organization
    "Q486972",    // human settlement
    "Q515",       // city
    "Q1093829",   // city in the United States
    "Q532",       // village
    "Q484170",    // commune of France
    "Q747074",    // comune of Italy
    "Q4022",      // river
    "Q8502",      // mountain
    "Q23397",     // lake
    "Q11446",     // ship
    "Q1248784",   // airport
    "Q3863",      // asteroid
    "Q618779",    // award
    "Q7187",      // gene
    "Q8054",      // protein
    "Q11173",     // chemical compound
};

// Ids (P..., Q...) whose presence in raw JSON makes an entity a candidate
const IdSet &candidateMarkers() {
    static const IdSet markers = [] {
        IdSet set;
        for (const auto *ids : {&languageCodeProps, &languageMetaclasses, &languageClasses, &lexicalClasses,
                                &lexicalSuperclasses, &grammaticalClasses, &linguisticsClasses,
                                &linguisticsSuperclasses, &linguisticsFields})
            set.insert(ids->begin(), ids->end());
        return set;
    }();
    return markers;
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

bool hasClaim(const json &entity, std::string_view prop) {
    auto claims = entity.find("claims");
    return claims != entity.end() && claims->is_object() && claims->contains(std::string(prop));
}

bool isItem(const json &entity) {
    return entity.value("type", "") == "item" && entity.contains("id");
}

} // namespace

bool WikiData::mayBeWiktNeeded(std::string_view title, std::string_view text) {
    if (explicitItems.contains(title) || title == languageItem || title == grammaticalItem || title == linguisticsItem)
        return true;
    // Scan quoted tokens like "Q34770" or "P220" (claim keys and item values)
    const auto &markers = candidateMarkers();
    for (size_t pos = text.find('"'); pos != std::string_view::npos; pos = text.find('"', pos + 1)) {
        if (pos + 2 >= text.size() || (text[pos + 1] != 'Q' && text[pos + 1] != 'P'))
            continue;
        size_t end = pos + 2;
        while (end < text.size() && text[end] >= '0' && text[end] <= '9')
            end++;
        if (end == pos + 2 || end >= text.size() || text[end] != '"')
            continue;
        if (markers.contains(text.substr(pos + 1, end - pos - 1)))
            return true;
        pos = end;
    }
    return false;
}

bool WikiData::isExcluded(const json &entity) {
    return !isItem(entity) || anyClaimIn(entity, "P31", excludedClasses);
}

bool WikiData::isExcludedTermMatch(const json &entity) {
    return isExcluded(entity) || anyClaimIn(entity, "P31", termExcludedClasses);
}

bool WikiData::isLanguage(const json &entity) {
    for (const auto &prop : languageCodeProps)
        if (hasClaim(entity, prop))
            return true;
    return anyClaimIn(entity, "P31", languageClasses) || anyClaimIn(entity, "P31", languageMetaclasses) ||
           anyClaimIn(entity, "P279", languageClasses) || entityId(entity) == languageItem;
}

bool WikiData::isLexical(const json &entity) {
    if (anyClaimIn(entity, "P31", lexicalClasses))
        return true;
    for (const auto &id : claimIds(entity, "P279"))
        if (lexicalClasses.contains(id) || lexicalSuperclasses.contains(id))
            return true;
    return explicitItems.contains(entityId(entity));
}

bool WikiData::isGrammatical(const json &entity) {
    return anyClaimIn(entity, "P31", grammaticalClasses) || entityId(entity) == grammaticalItem;
}

bool WikiData::isLinguistics(const json &entity) {
    if (anyClaimIn(entity, "P279", linguisticsClasses) || anyClaimIn(entity, "P31", linguisticsClasses) ||
        anyClaimIn(entity, "P279", linguisticsSuperclasses) || anyClaimIn(entity, "P2579", linguisticsFields) ||
        entityId(entity) == linguisticsItem)
        return true;
    static const IdSet partOf = {"Q34770", "Q8162"}; // language, linguistics
    return anyClaimIn(entity, "P361", partOf);
}

bool WikiData::isWiktNeeded(const json &entity) {
    if (isExcluded(entity))
        return false;
    return isLanguage(entity) || isLexical(entity) || isGrammatical(entity) || isLinguistics(entity);
}
