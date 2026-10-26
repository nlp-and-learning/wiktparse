#include "HeadingTranslator.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <sqlite3.h>

namespace {

sqlite3 *openReadOnly(const QString &path) {
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    return db;
}

// Rows of a query with text parameters, each a list of its columns
QList<QStringList> query(sqlite3 *db, const char *sql, const QStringList &parameters = {}) {
    QList<QStringList> rows;
    sqlite3_stmt *stmt = nullptr;
    if (!db || sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return rows;
    for (int i = 0; i < parameters.size(); ++i)
        sqlite3_bind_text(stmt, i + 1, parameters[i].toUtf8().constData(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QStringList row;
        for (int c = 0; c < sqlite3_column_count(stmt); ++c)
            row << QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, c)));
        rows << row;
    }
    sqlite3_finalize(stmt);
    return rows;
}

// Markup of a heading text left out: [[link|text]] as text, '' and ''', <small>
QString plain(QString text) {
    static const QRegularExpression link(R"(\[\[(?:[^\]|]*\|)?([^\]|]*)\]\])"), tags(R"(<[^>]*>|''+)");
    text.replace(link, R"(\1)");
    text.remove(tags);
    return text.trimmed();
}

} // namespace

HeadingTranslator::~HeadingTranslator() {
    sqlite3_close(langs_);
    sqlite3_close(sections_);
}

bool HeadingTranslator::open(const QString &dir) {
    QDir at(dir);
    for (int up = 0; up < 3; ++up) {
        if (QFileInfo::exists(at.filePath("wiktlangs.sqlite")) && QFileInfo::exists(at.filePath("wiktsections.sqlite"))) {
            if (at.absolutePath() == dir_ && isOpen())
                return true;
            sqlite3_close(langs_);
            sqlite3_close(sections_);
            langs_ = openReadOnly(at.filePath("wiktlangs.sqlite"));
            sections_ = openReadOnly(at.filePath("wiktsections.sqlite"));
            dir_ = at.absolutePath();
            wikis_.clear();
            languageNames_.clear();
            sectionNames_.clear();
            codes_.clear();
            for (const auto &row : query(langs_, "SELECT code FROM language"))
                codes_.insert(row[0]);
            return isOpen();
        }
        if (!at.cdUp())
            break;
    }
    return isOpen();
}

const HeadingTranslator::Wiki &HeadingTranslator::wikiData(const QString &wiki) {
    if (auto it = wikis_.constFind(wiki); it != wikis_.constEnd())
        return *it;
    QString lang = wiki == "simple" ? QStringLiteral("en") : wiki;
    lang.replace('_', '-');
    Wiki w;
    for (const auto &row : query(langs_, "SELECT name, code FROM language_name WHERE in_code=?1", {lang}))
        w.languages.insert(row[0].toLower(), row[1]);
    // as wikt2jsonl: the wiki's own names (by hand), Wikidata names in its language, English ones
    for (const auto &row : query(sections_,
                                 "SELECT name, section FROM section_name WHERE lang=?1 AND source='manual' "
                                 "UNION ALL SELECT name, section FROM section_name WHERE lang=?2 "
                                 "UNION ALL SELECT section, section FROM section",
                                 {wiki, lang}))
        if (!w.sections.contains(row[0].toLower()))
            w.sections.insert(row[0].toLower(), row[1]);
    return *wikis_.insert(wiki, w);
}

QString HeadingTranslator::sectionOf(const Wiki &w, const QString &candidate) const {
    return w.sections.value(candidate.toLower());
}

QString HeadingTranslator::languageOf(const Wiki &w, const QString &candidate) const {
    if (codes_.contains(candidate))
        return candidate;
    return w.languages.value(candidate.toLower());
}

QString HeadingTranslator::languageName(const QString &code, const QString &display) {
    if (display.isEmpty())
        return code;
    const QString key = code + '|' + display;
    if (auto it = languageNames_.constFind(key); it != languageNames_.constEnd())
        return *it;
    // The label (ja 英語), or its last word when that is a name too (pl "język polski" -> "polski")
    const auto rows = query(langs_,
                            "SELECT name, source FROM language_name WHERE code=?1 AND in_code=?2 "
                            "ORDER BY source='wikidata-label' DESC, length(name)",
                            {code, display});
    QString name = rows.isEmpty() ? code : rows[0][0];
    if (const auto space = name.lastIndexOf(' '); space > 0)
        for (const auto &row : rows)
            if (row[0] == name.mid(space + 1))
                name = row[0];
    return *languageNames_.insert(key, name);
}

QString HeadingTranslator::sectionName(const QString &section, const QString &display) {
    // (English: the section itself, the Wikidata labels are lowercase)
    if (display.isEmpty() || display == "en")
        return section;
    const QString key = section + '|' + display;
    if (auto it = sectionNames_.constFind(key); it != sectionNames_.constEnd())
        return *it;
    const auto rows = query(sections_,
                            "SELECT name FROM section_name WHERE section=?1 AND lang=?2 "
                            "ORDER BY source='wikidata-label' DESC, source='manual' DESC LIMIT 1",
                            {section, display});
    return *sectionNames_.insert(key, rows.isEmpty() ? section : rows[0][0]);
}

// A heading text (or the inside of a template line) translated; empty when nothing is known
QString HeadingTranslator::translateText(const Wiki &w, const QString &text, const QString &display, bool parameters) {
    // pl "woda ({{język polski}})", de "Wasser ({{Sprache|Deutsch}})": the language in parentheses
    static const QRegularExpression wordAndLanguage(R"(^(.*\S)\s*\((.*(?:\{\{|\[\[).*)\)\s*$)");
    if (const auto m = wordAndLanguage.match(text); m.hasMatch())
        if (const QString inner = translateText(w, m.captured(2), display); !inner.isEmpty())
            return m.captured(1) + " (" + inner + ")";
    // "Etymology 2", "Znaczenie 1": the number kept
    static const QRegularExpression numbered(R"(^(.*\S)\s+(\d+)$)");
    if (const auto m = numbered.match(text); m.hasMatch())
        if (const QString base = translateText(w, m.captured(1), display); !base.isEmpty())
            return base + ' ' + m.captured(2);
    // candidates: the parameters and the name of a template ({{S|nom|fr}}, {{langue|fr}}, {{-it-}}), else the text;
    // of a heading starting with a template, that one (de "{{Wortart|Substantiv|Deutsch}}, {{n}}")
    QStringList candidates;
    bool languageFirst = false, fromTemplate = false;
    static const QRegularExpression single(R"(^\{\{([^{}]*)\}\})");
    if (const auto m = single.match(text.trimmed()); m.hasMatch()) {
        const QStringList parts = m.captured(1).split('|');
        // one parameter: a language ({{langue|fr}}, {{Sprache|Deutsch}}); more: a section first ({{S|nom|fr}})
        languageFirst = parameters && parts.size() == 2;
        fromTemplate = true;
        for (int i = 1; parameters && i < parts.size(); ++i)
            if (!parts[i].contains('='))
                candidates << parts[i].trimmed();
        QString name = parts[0].trimmed();
        candidates << name;
        while (!name.isEmpty() && (name.front() == '-' || name.front() == '='))
            name.remove(0, 1);
        while (!name.isEmpty() && (name.back() == '-' || name.back() == '='))
            name.chop(1);
        candidates << name;
    } else {
        candidates << plain(text);
    }
    // (no letters: not a name, fr {{)}})
    static const QRegularExpression letter(R"(\p{L})");
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [](const QString &c) { return !c.contains(letter); }),
                     candidates.end());
    // a section first ({{S|nom|fr}}: "nom" is a noun, not the code of Nomatsiguenga), then a language
    auto section = [&]() -> QString {
        for (const QString &c : candidates)
            if (const QString s = sectionOf(w, c); !s.isEmpty())
                return sectionName(s, display);
        return {};
    };
    // (a code in a template stays as written: {{langue|fr}}, {{-it-}}; names are translated:
    // "język angielski", "English", {{język polski}})
    auto language = [&]() -> QString {
        for (const QString &c : candidates)
            if (const QString code = languageOf(w, c); !code.isEmpty())
                return fromTemplate && code == c ? text.trimmed() : languageName(code, display);
        return {};
    };
    if (languageFirst) {
        if (const QString l = language(); !l.isEmpty())
            return l;
        return section();
    }
    if (const QString s = section(); !s.isEmpty())
        return s;
    return language();
}

QString HeadingTranslator::translate(const QString &text, const QString &wiki, const QString &display) {
    if (!isOpen())
        return text;
    const Wiki &w = wikiData(wiki);
    static const QRegularExpression heading(R"(^(=+)\s*(.*?)\s*(=+)\s*$)"), templateLine(R"(^\s*(\{\{[^{}]*\}\})\s*$)");
    QStringList lines = text.split('\n');
    for (QString &line : lines) {
        if (const auto m = heading.match(line); m.hasMatch() && !m.captured(2).isEmpty()) {
            if (const QString t = translateText(w, m.captured(2), display); !t.isEmpty())
                line = m.captured(1) + ' ' + t + ' ' + m.captured(3);
        } else if (const auto t = templateLine.match(line); t.hasMatch()) {
            // a line of a template: by its name only ({{znaczenia}}, {{-it-}}; not {{IPA|pl|…}})
            if (const QString r = translateText(w, t.captured(1), display, false); !r.isEmpty())
                line = "{{" + r + "}}";
        }
    }
    return lines.join('\n');
}
