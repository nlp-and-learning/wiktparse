#pragma once
// Headings of a wiki's wikitext in a common form, to compare the structure of Wiktionaries in
// other languages and scripts: a language heading (== Polski ==, == {{langue|fr}} ==,
// == woda ({{język polski}}) ==) as its code (wiktlangs.sqlite), a section heading or a
// section template line (=== Rzeczownik ===, {{znaczenia}}) as its English section
// (wiktsections.sqlite); with a display language, as their names in it, falling back to the
// code and the English section.
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

struct sqlite3;

class HeadingTranslator {
public:
    HeadingTranslator() = default;
    ~HeadingTranslator();
    HeadingTranslator(const HeadingTranslator &) = delete;
    HeadingTranslator &operator=(const HeadingTranslator &) = delete;

    // The two databases in dir (or its parents, two levels up); false when either is missing
    bool open(const QString &dir);
    [[nodiscard]] bool isOpen() const { return langs_ && sections_; }
    [[nodiscard]] QString databaseDir() const { return dir_; }

    // The wikitext of a page of wiki, its headings and section template lines translated;
    // display: a language code for names, empty for codes and English sections
    QString translate(const QString &text, const QString &wiki, const QString &display);

private:
    struct Wiki {
        QHash<QString, QString> languages; // lowercase name in the wiki's language (and aliases) -> code
        QHash<QString, QString> sections;  // lowercase name -> English section
        // of the wiki's configuration (res/wiktionary/<wiki>.json), as wikt2jsonl
        QString lookup;                    // ar "ال{name}"
        QStringList strip;                 // ar "لغة": words left out of names
        QRegularExpression templateRegex;  // lt "^([a-z]{2,3})v$": the code in a template name
    };
    // The code of a template name by the wiki's template_regex (lt "ltv" -> lt), or empty
    QString templateCode(const Wiki &w, const QString &name) const;
    // A code of another scheme as the canonical one (mg "zho" -> zh), or empty
    QString canonicalCode(const QString &code) const;
    const Wiki &wikiData(const QString &wiki);
    // any: by the names of any wiki or language (unambiguous ones), not of this wiki
    QString languageOf(const Wiki &w, const QString &candidate, bool any) const;
    QString sectionOf(const Wiki &w, const QString &candidate, bool any) const;
    QString languageName(const QString &code, const QString &display);
    QString sectionName(const QString &section, const QString &display);
    // parameters: also the parameters of a template ({{langue|fr}}), not only its name
    QString translateText(const Wiki &w, const QString &text, const QString &display, bool parameters = true);

    sqlite3 *langs_ = nullptr, *sections_ = nullptr;
    QString dir_;
    QSet<QString> codes_;            // language codes of wiktlangs
    QHash<QString, QString> codeMap_; // other codes (ISO 639-3 zho) -> canonical (zh)
    QString configDir_;              // res/wiktionary of the repository, when found
    QHash<QString, Wiki> wikis_;
    QHash<QString, QString> anySection_, anyLanguage_; // lowercase name of any wiki or language, if unambiguous
    QHash<QString, QString> languageNames_, sectionNames_; // "code|display" -> name
};
