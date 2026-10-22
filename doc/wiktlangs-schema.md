# wiktlangs.sqlite: planned layout

Today the tables grew one source at a time and repeat each other: `lang` (en.wikt
modules), `lang_alias`, `lang_name` (Wikidata labels), `wd_code_map`,
`wikt_marker` (headings of Wiktionaries), `wikt_lang_name` (Lua modules of
Wiktionaries), plus aliases in `res/wiktionary/<wiki>.json` and
`res/extra_languages.tsv` that are not in the database at all. Planned:

## language: one row per language
`code` (canonical: en.wikt code, or a private `art-…` code), `iso639_1`,
`iso639_3`, `glottolog`, `wikimedia` (codes of other schemes, like the
Wikipedia infobox fields), `qid`, `kind` (language, etymology, family,
script, extra), `family`, `parent`, `ancestors`, `scripts`, `source`.

## language_name: every name of a language in another language
`code` (the language named), `in_code` (canonical code of the language the
name is written in), `name`, `source` (en.wikt, wikidata-label,
wikidata-alias, wikt-module, wikt-heading, config-alias), `wiki` (the
Wiktionary it was seen in, when from a Wiktionary), `count` (sections,
for headings).

So "język wietnamski" is (`vi`, `pl`, `język wietnamski`, wikt-heading, pl,
5231) and "Vietnamese" is (`vi`, `en`, `Vietnamese`, en.wikt, ...). Name
lookup of a wiki is then one query: `in_code` = language of the wiki.

## Moved out
- Section names: `wiktsections.sqlite` (done: `wikt_section` removed from
  wiktlangs).
- Configuration aliases become rows of `language_name` (source
  config-alias) instead of living only in JSON.
