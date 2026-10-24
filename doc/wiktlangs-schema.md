# wiktlangs.sqlite

Built by `wiktlangs`. All codes are canonical: en.wikt codes (languages,
etymology languages, families, scripts), private codes of
`res/extra_languages.tsv` (`art-slovio`), and for languages of English
Wikipedia infoboxes unknown to en.wikt an ISO 639-3 or Glottolog code (kind
`wikipedia`).

## language: one row per language
| column | |
|---|---|
| `code` | canonical code (primary key) |
| `name` | English name, as en.wikt writes it, without "language" |
| `kind` | `language`, `etymology` (variety, historical stage), `family`, `script`, `extra`, `wiktionary` (languages of language modules of other Wiktionaries, unknown to en.wikt: ISO 639-3 codes such as fr `aao` arabe saharien, or the wiki's own codes as `<wiki>:<code>`, fr `fr:proto-khasique`, named in the wiki's language), `wikipedia` |
| `iso639_1`, `iso639_3` | ISO codes: of the code itself (SIL tables), else of the Wikidata item |
| `glottolog` | Glottolog code: of the Wikidata item, else by ISO 639-3 |
| `wikimedia` | Wikimedia code of the Wikidata item (P424) |
| `qid` | Wikidata item |
| `family`, `parent`, `ancestors`, `scripts`, `type` | from en.wikt modules |
| `source` | module or file the row comes from |

The English name is also a row of `language_name` (redundant on purpose: the
row of `language` is readable by itself).

## language_name: names of languages in languages
| column | |
|---|---|
| `code` | the language named |
| `in_code` | the language the name is written in |
| `name` | |
| `source` | `en.wikt` (canonical English names), `en.wikt-alias`, `en.wikt-variety`, `extra`, `enwiki` (infobox names), `sil` (English names of `wiktionary` languages), `wikidata-label`, `wikidata-alias`, `wikidata-other` (labels and aliases of another Wikidata item of the language, from its English Wikipedia infobox: Griko Q35719 for `grk-ita`), `wikt-module` (Lua modules of Wiktionaries, `wiktmodules`), `wikt-heading` (resolved language headings of Wiktionaries, `wikt2jsonl` markers), `config-alias` (`res/wiktionary/<wiki>.json`) |

One row per (`code`, `in_code`, `name`), with its best source in the order
above. So "język wietnamski" is (`vi`, `pl`, `język wietnamski`,
`wikidata-label`). Name lookup of a wiki is one query: `in_code` = language
of the wiki. Label languages and wikis that are no canonical codes are
mapped: `zh-hant` → `zh`, `simple` → `en`, `no` → `nb`, `diq` → `zza`
(macrolanguage, also noted in `code_map`).

## code_map: codes of other schemes
| column | |
|---|---|
| `scheme` | `iso639-1`, `iso639-2`, `iso639-3`, `wikimedia`, `glottolog`, `wikt-<wiki>` (a Wiktionary's own codes: fr `eudeve` → `opt`, `proto-khasique` → `fr:proto-khasique`) |
| `code` | |
| `language` | canonical code |
| `method` | `item` (a code of the language's row), `code` (the same code), `iso1`, `qid` (Wikidata item of an en.wikt language), `macrolanguage`, `retired` (replacement of a retired ISO code), `name` (a wiki's own code by its name: in the wiki's language, else English; one language, or the only one of kind `language`; a one-word name also as a whole part of names), `wiki` (a wiki's own code made a language `<wiki>:<code>`) |

A code may map to several languages (`quz` → `qu` by macrolanguage and to the
infobox language `cusc1236`); readers prefer kind `language`.

## Moved out
- Section names: `wiktsections.sqlite`.
