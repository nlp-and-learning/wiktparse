#include "Plain.h"

#include <QRegularExpression>

QString withoutLinksAndTemplates(const QString &text) {
    if (!text.contains("[[") && !text.contains("{{"))
        return text;
    // [[target|shown]], [[target]] of no namespace
    static const QRegularExpression piped(R"(\[\[[^\[\]|:]+\|([^\[\]]*)\]\])"), plain(R"(\[\[([^\[\]|:]+)\]\])");
    // {{name|value}}: one parameter, no name (no =), no inner template; not a parser function {{#if:…}}
    static const QRegularExpression single(R"(\{\{\s*[^{}|#=\s][^{}|]*\|([^{}|=]*)\}\})");
    QString result = text;
    for (QString before; before != result;) {
        before = result;
        result.replace(piped, "\\1").replace(plain, "\\1").replace(single, "\\1");
    }
    return result;
}
