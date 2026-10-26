#include "Latin.h"

#include <QHash>

namespace {

enum class Kind {
    Consonant, // б в г …: softened before е ё ю я, и, ь
    Hard,      // ж ш ц: never soft (жи -> ży, же -> że)
    Soft,      // ч щ й: soft of themselves (че -> cze), no i added
    Vowel,     // а о у ы э …
    Iotated,   // е ё ю я є: j + vowel, the vowel alone after a consonant
    I,         // и і: y after ж ш ц
    SoftSign,  // ь: softens the consonant before it
    HardSign,  // ъ: nothing
};

struct Letter {
    const char *latin; // as written alone (of an iotated vowel: without its j)
    Kind kind;
    const char *soft = nullptr; // a consonant before ь (сь -> ś); else latin + '
};

const QHash<char16_t, Letter> &letters() {
    static const QHash<char16_t, Letter> table = {
            // Russian
            {u'а', {"a", Kind::Vowel}},
            {u'б', {"b", Kind::Consonant}},
            {u'в', {"w", Kind::Consonant}},
            {u'г', {"g", Kind::Consonant}},
            {u'д', {"d", Kind::Consonant, "dź"}},
            {u'е', {"e", Kind::Iotated}},
            {u'ё', {"o", Kind::Iotated}},
            {u'ж', {"ż", Kind::Hard}},
            {u'з', {"z", Kind::Consonant, "ź"}},
            {u'и', {"i", Kind::I}},
            {u'й', {"j", Kind::Soft}},
            {u'к', {"k", Kind::Consonant}},
            {u'л', {"ł", Kind::Consonant, "l"}},
            {u'м', {"m", Kind::Consonant}},
            {u'н', {"n", Kind::Consonant, "ń"}},
            {u'о', {"o", Kind::Vowel}},
            {u'п', {"p", Kind::Consonant}},
            {u'р', {"r", Kind::Consonant}},
            {u'с', {"s", Kind::Consonant, "ś"}},
            {u'т', {"t", Kind::Consonant}},
            {u'у', {"u", Kind::Vowel}},
            {u'ф', {"f", Kind::Consonant}},
            {u'х', {"ch", Kind::Consonant}},
            {u'ц', {"c", Kind::Hard, "ć"}},
            {u'ч', {"cz", Kind::Soft}},
            {u'ш', {"sz", Kind::Hard}},
            {u'щ', {"szcz", Kind::Soft}},
            {u'ъ', {"", Kind::HardSign}},
            {u'ы', {"y", Kind::Vowel}},
            {u'ь', {"", Kind::SoftSign}},
            {u'э', {"e", Kind::Vowel}},
            {u'ю', {"u", Kind::Iotated}},
            {u'я', {"a", Kind::Iotated}},
            // Ukrainian, Belarusian
            {u'і', {"i", Kind::I}},
            {u'ї', {"ji", Kind::Vowel}},
            {u'є', {"e", Kind::Iotated}},
            {u'ґ', {"g", Kind::Consonant}},
            {u'ў', {"ł", Kind::Consonant}},
            // Serbian, Macedonian
            {u'ђ', {"dź", Kind::Soft}},
            {u'ј', {"j", Kind::Soft}},
            {u'љ', {"l", Kind::Soft}},
            {u'њ', {"ń", Kind::Soft}},
            {u'ћ', {"ć", Kind::Soft}},
            {u'џ', {"dż", Kind::Hard}},
            {u'ѓ', {"gi", Kind::Soft}},
            {u'ќ', {"ki", Kind::Soft}},
            {u'ѕ', {"dz", Kind::Consonant}},
            // Kazakh, Tatar, Bashkir, Tajik, Kyrgyz, Mongolian…
            {u'ә', {"e", Kind::Vowel}},
            {u'ғ', {"g", Kind::Consonant}},
            {u'қ', {"k", Kind::Consonant}},
            {u'ң', {"ng", Kind::Consonant}},
            {u'ө', {"o", Kind::Vowel}},
            {u'ү', {"u", Kind::Vowel}},
            {u'ұ', {"u", Kind::Vowel}},
            {u'һ', {"h", Kind::Consonant}},
            {u'ҳ', {"h", Kind::Consonant}},
            {u'ӣ', {"i", Kind::Vowel}},
            {u'ӯ', {"u", Kind::Vowel}},
            {u'ҷ', {"dż", Kind::Hard}},
            {u'җ', {"dż", Kind::Hard}},
            {u'ҙ', {"z", Kind::Consonant}},
            {u'ҫ', {"s", Kind::Consonant}},
            {u'ҡ', {"k", Kind::Consonant}},
            {u'ӑ', {"a", Kind::Vowel}},
            {u'ӗ', {"e", Kind::Vowel}},
            {u'ӳ', {"u", Kind::Vowel}},
            {u'ӱ', {"u", Kind::Vowel}},
            {u'ӧ', {"o", Kind::Vowel}},
            {u'ӓ', {"a", Kind::Vowel}},
            {u'ӏ', {"", Kind::HardSign}},
    };
    return table;
}

const Letter *letterOf(QChar c) {
    const auto &table = letters();
    auto it = table.find(c.toLower().unicode());
    return it == table.end() ? nullptr : &*it;
}

bool isConsonant(const Letter *l) {
    return l && (l->kind == Kind::Consonant || l->kind == Kind::Hard || l->kind == Kind::Soft);
}

} // namespace

QString cyrillicToLatin(const QString &text) {
    QString result;
    result.reserve(text.size());
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = text[i];
        const Letter *l = letterOf(c);
        if (!l) {
            result += c;
            continue;
        }
        const Letter *before = i > 0 ? letterOf(text[i - 1]) : nullptr;
        const Letter *after = i + 1 < n ? letterOf(text[i + 1]) : nullptr;
        QString out = QString::fromUtf8(l->latin);
        switch (l->kind) {
        case Kind::Consonant:
            if (after && after->kind == Kind::SoftSign)
                out = l->soft ? QString::fromUtf8(l->soft) : out + "'";
            else if (after && after->kind == Kind::Iotated)
                out = out == "ł" ? "l" : out + "i"; // ня -> nia, ля -> la
            else if (after && after->kind == Kind::I && out == "ł")
                out = "l"; // ли -> li
            break;
        case Kind::Hard:
            if (after && after->kind == Kind::SoftSign && l->soft)
                out = QString::fromUtf8(l->soft);
            break;
        case Kind::Iotated:
            if (!isConsonant(before))
                out = "j" + out; // at the start, after a vowel, ь, ъ: я -> ja
            break;
        case Kind::I:
            if (before && before->kind == Kind::Hard)
                out = "y"; // жи -> ży
            break;
        default:
            break;
        }
        if (c.isUpper() && !out.isEmpty()) {
            // a word in capitals all of it, else the first letter
            const bool capitals = (i + 1 < n && text[i + 1].isUpper() && letterOf(text[i + 1])) ||
                                  (i > 0 && text[i - 1].isUpper() && letterOf(text[i - 1]));
            out = capitals ? out.toUpper() : out.left(1).toUpper() + out.mid(1);
        }
        result += out;
    }
    return result;
}
