#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

#include "WikiTerms.h"
#include "wikitext/HeaderTerms.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

TEST(HeaderTermsTest, TextAndTemplates) {
    auto t = tokenizeHeader("Etymology");
    ASSERT_EQ(t.texts.size(), 1u);
    EXPECT_EQ(t.texts[0], "Etymology");
    EXPECT_TRUE(t.templates.empty());

    t = tokenizeHeader("{{S|verbe|sco|num=1}}");
    ASSERT_EQ(t.templates.size(), 1u);
    EXPECT_EQ(t.templates[0].name, "S");
    EXPECT_EQ(t.templates[0].args, (std::vector<std::string>{"verbe", "sco"}));
    EXPECT_TRUE(t.texts.empty());

    t = tokenizeHeader("{{Wortart|Substantiv|Deutsch}}, {{f}}");
    ASSERT_EQ(t.templates.size(), 2u);
    EXPECT_EQ(t.templates[0].args, (std::vector<std::string>{"Substantiv", "Deutsch"}));
    EXPECT_EQ(t.templates[1].name, "f");
}

TEST(HeaderTermsTest, PlainTextKeepsMultibyteCharacters) {
    EXPECT_EQ(headerPlainText("[[跨语言|跨语言]]"), "跨语言");
    EXPECT_EQ(headerPlainText("— ''Noun'' (1):"), "Noun (1");
    EXPECT_EQ(headerPlainText("ကြိယာ —"), "ကြိယာ");
}

TEST(HeaderTermsTest, PlainTextDecodesEntitiesAndTrimsBrokenMarkup) {
    EXPECT_EQ(headerPlainText("Créole réunionnais&nbsp;"), "Créole réunionnais");
    EXPECT_EQ(headerPlainText("/Créole réunionnais"), "Créole réunionnais");
    EXPECT_EQ(headerPlainText("== عَرَبِيَّة =="), "عَرَبِيَّة");
    EXPECT_EQ(headerPlainText("Sinn &amp; Form"), "Sinn & Form");
    EXPECT_EQ(headerPlainText("caf&#233;"), "café");
    EXPECT_EQ(headerPlainText("a&zwnj;b"), "ab");
}

TEST(HeaderTermsTest, LanguageCodes) {
    EXPECT_TRUE(looksLikeLanguageCode("fr"));
    EXPECT_TRUE(looksLikeLanguageCode("sco"));
    EXPECT_TRUE(looksLikeLanguageCode("roa-oca"));
    EXPECT_FALSE(looksLikeLanguageCode("verbe"));
    EXPECT_FALSE(looksLikeLanguageCode("Noun"));
}

TEST(WikiTermsTest, UnescapeJson) {
    EXPECT_EQ(WikiTerms::unescapeJson(R"(За)"), "За");
    EXPECT_EQ(WikiTerms::unescapeJson(R"(a\"b\\c)"), "a\"b\\c");
    EXPECT_EQ(WikiTerms::unescapeJson(R"(😀)"), "😀");
    EXPECT_EQ(WikiTerms::unescapeJson("plain"), "plain");
}

TEST(WikiTermsTest, MatchesLabelsInWiktionaryLanguage) {
    auto dir = fs::temp_directory_path() / "wiktparse-terms-test";
    fs::create_directories(dir);
    std::ofstream(dir / "en.txt") << "Noun\nEtymology\n";
    std::ofstream(dir / "fr.txt") << "verbe\n";
    std::ofstream(dir / "zh_min_nan.txt") << "Miâ-sû\n";
    WikiTerms terms;
    EXPECT_EQ(terms.load(dir), 4u);
    fs::remove_all(dir);

    // Wikidata dump escapes non-ASCII characters
    std::string noun = R"({"type":"item","id":"Q1084","labels":{"en":{"language":"en","value":"noun"},)"
                       R"("de":{"language":"de","value":"Substantiv"}},"aliases":{},"claims":{}})";
    EXPECT_TRUE(terms.mayMatch(noun));
    auto matched = terms.matches(json::parse(noun));
    ASSERT_EQ(matched.size(), 1u);
    EXPECT_EQ(matched[0], (std::pair<std::string, std::string>{"en", "noun"}));

    std::string verb = R"({"type":"item","id":"Q24905","labels":{"en":{"language":"en","value":"verb"}},)"
                       R"("aliases":{"fr":[{"language":"fr","value":"verbe"}]}})";
    EXPECT_TRUE(terms.mayMatch(verb));
    EXPECT_EQ(terms.matches(json::parse(verb)).size(), 1u);

    // Label language differs from wiki code; escaped value
    std::string nan = R"({"labels":{"nan":{"language":"nan","value":"miâ-sû"}}})";
    EXPECT_TRUE(terms.mayMatch(nan));

    // Term in another language than the label does not match
    std::string other = R"({"labels":{"de":{"language":"de","value":"verbe"}}})";
    EXPECT_FALSE(terms.mayMatch(other));
    EXPECT_TRUE(terms.matches(json::parse(other)).empty());
}
