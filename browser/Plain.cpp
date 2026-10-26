#include "Plain.h"

#include <QHash>
#include <QRegularExpression>

namespace {

// HTML entities as their characters (&#171; «, &#x2192; →, &nbsp;, &lt;); unknown ones as they are
QString withDecodedEntities(const QString &text) {
    if (!text.contains('&'))
        return text;
    static const QHash<QString, QString> named = {
            {"nbsp", QString(QChar(0x00A0))}, {"lt", "<"}, {"gt", ">"}, {"amp", "&"}, {"quot", "\""}, {"apos", "'"},
            {"laquo", "«"}, {"raquo", "»"}, {"ndash", "–"}, {"mdash", "—"}, {"hellip", "…"}, {"middot", "·"},
            {"bull", "•"}, {"deg", "°"}, {"times", "×"}, {"minus", "−"}, {"shy", QString(QChar(0x00AD))},
            {"lrm", QString(QChar(0x200E))}, {"rlm", QString(QChar(0x200F))}, {"zwj", QString(QChar(0x200D))},
            {"zwnj", QString(QChar(0x200C))}, {"thinsp", QString(QChar(0x2009))}, {"larr", "←"}, {"rarr", "→"},
            {"uarr", "↑"}, {"darr", "↓"}, {"harr", "↔"}, {"lArr", "⇐"}, {"rArr", "⇒"}, {"hArr", "⇔"},
            {"bdquo", "„"}, {"rdquo", "”"}, {"ldquo", "“"}, {"lsquo", "‘"}, {"rsquo", "’"}, {"sect", "§"},
            {"copy", "©"}, {"reg", "®"}};
    static const QRegularExpression entity(R"(&(#[0-9]{1,7}|#[xX][0-9A-Fa-f]{1,6}|[A-Za-z]{2,8});)");
    QString result;
    qsizetype last = 0;
    for (auto it = entity.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString name = m.captured(1);
        QString decoded;
        if (name.startsWith('#')) {
            bool ok = false;
            const uint code = name.size() > 1 && (name[1] == 'x' || name[1] == 'X') ? name.mid(2).toUInt(&ok, 16)
                                                                                    : name.mid(1).toUInt(&ok, 10);
            if (ok && code > 0 && code <= 0x10FFFF)
                decoded = QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1);
        } else if (auto found = named.find(name); found != named.end()) {
            decoded = *found;
        }
        if (decoded.isEmpty())
            continue;
        result += text.mid(last, m.capturedStart() - last) + decoded;
        last = m.capturedEnd();
    }
    return result + text.mid(last);
}

} // namespace

QString withoutLinksAndTemplates(const QString &text) {
    if (!text.contains("[[") && !text.contains("{{"))
        return withDecodedEntities(text);
    // [[target|shown]], [[target]] of no namespace
    static const QRegularExpression piped(R"(\[\[[^\[\]|:]+\|([^\[\]]*)\]\])"), plain(R"(\[\[([^\[\]|:]+)\]\])");
    // {{name|value}}: one parameter, no name (no =), no inner template; not a parser function {{#if:…}}
    static const QRegularExpression single(R"(\{\{\s*[^{}|#=\s][^{}|]*\|([^{}|=]*)\}\})");
    QString result = text;
    for (QString before; before != result;) {
        before = result;
        result.replace(piped, "\\1").replace(plain, "\\1").replace(single, "\\1");
    }
    // (after the markup: &#124; is no | of a template)
    return withDecodedEntities(result);
}
