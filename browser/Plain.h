#pragma once
// Wikitext as its shown words: a link as its text ([[вода|воде]] -> воде, [[завод]] -> завод; not
// links of a namespace, files and categories), a template of one unnamed parameter as that
// parameter ({{выдел|водата}} -> водата; not {{t|en|water}}, {{m}}), inner ones first; then HTML
// entities as their characters (&#171;@&#187; -> «@», &nbsp;).
#include <QString>

QString withoutLinksAndTemplates(const QString &text);
