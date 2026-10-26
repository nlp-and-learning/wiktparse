#pragma once
// Cyrillic as its approximate sound in Latin letters, the Polish way (вода -> woda, щука -> szczuka,
// жёлтый -> żołtyj, люди -> ludi, семья -> siem'ja): a letter by the letter after it (е ё ю я, и,
// ь soften the consonant before them; е ё ю я lose their j after a consonant), after the table of
// the converter KonwPl; letters of other Cyrillic alphabets (Ukrainian, Serbian, Kazakh…) roughly.
// Text of no Cyrillic is as it is.
#include <QString>

QString cyrillicToLatin(const QString &text);
