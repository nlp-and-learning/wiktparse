# The uniform tree: one JSON shape for every Wiktionary

Polish version: `uniform-tree.pl.md`; change both together.

Draft of the format of branch `uniform-tree` (after v1.1.0). Every Wiktionary,
whatever its own layout, gives pages of the same shape, so that one program
reads all of them. This document describes the shape for people; the JSON
Schema follows it (`res/schema/`).

## Why

Up to v1.1.0 a page keeps the tree of the wiki's own headings: en
`Etymology 1 > Noun > Translations`, pl `Definitions > ''rzeczownik''`
(senses under a definitions section, the part of speech an italic line), ru
`Семантические свойства > Значение`, ar senses right under the language
heading, kbd and nia with no part of speech at all. Translations stand under
the part of speech (en, fr, de) or at the language level (pl, ru, it, nl).
Every reader has to know each layout.

The uniform tree keeps what is common to all wikis as fixed levels, and
everything else as attributes.

## The levels

```
page
└─ languages[]              one per language section of the page
   └─ entries[]             one per part of speech (and etymology, and form)
      ├─ senses[]           the definitions, ids "1", "1.2" (nesting as ids, not levels)
      ├─ translations[]     of this entry, each to a sense id when the wiki says which
      └─ relations{}        Synonyms, Antonyms, Hypernyms, Derived terms… lists of terms
```

Three levels only: **language**, **entry**, **sense**. The wiki's own
headings are not levels: what they say becomes attributes of an entry.

### Why the part of speech, not the etymology, below the language

- Every wiki gives a part of speech (a heading, a template, an italic line), or
  one can be told from an inflection template; where none can, the entry has
  `pos: null`. The level is always there.
- Only some wikis split by etymology (en `Etymology 1/2`, es `Etimología 1`,
  de one heading per homograph, ru `{{з|I}}`). As a level it would be empty or
  made up in most wikis. As an attribute nothing is lost: en *rock* is four
  entries (noun 1, verb 1, verb 2, noun 3) with `etymology` 1, 1, 2, 3.

So an **entry** is a group of senses of one part of speech, of one etymology
when the wiki tells it, of one form when the wiki tells it (pl *bić* and *bić
się* are two entries, both verbs). One part of speech may come in several
entries.

## Page

| field | meaning |
|---|---|
| `wiki` | the Wiktionary (`en`, `pl`, `zh_min_nan`) |
| `title` | the page title |
| `ns` | the namespace (0 main; 110 Thesaurus in en…) |
| `source` | where the page is in the dump: `[stream offset, page number in the stream]` |
| `languages` | the language sections, in page order |

## Language

| field | meaning |
|---|---|
| `code` | canonical language code (wiktlangs.sqlite), `null` when unknown |
| `language` | the language as written in the heading or marker (`język polski`, `{{langue|fr}}`, `Ingiliffaa`) |
| `native` | the language of the wiki (an entry of the wiki's own language) |
| `pronunciation` | IPA, audio files, hyphenation of the whole language section, when the wiki gives them there |
| `etymology` | the etymology text when the wiki gives one for the whole section |
| `entries` | the entries |
| `notes` | sections of the language that belong to no entry (References, See also, External links…): `{section: text}` |

## Entry

| field | meaning |
|---|---|
| `pos` | the part of speech, an English section name of wiktsections.sqlite (`Noun`, `Verb`, `Proper noun`, `Han character`); `null` when the wiki gives none |
| `pos_written` | the part of speech as written (`''rzeczownik, rodzaj żeński''`, `{{S|nom|fr}}`, `{{-ana-|mg}}`) |
| `etymology` | the etymology of this entry: `{"n": 2, "text": "…"}`; `n` the number the wiki gives (en `Etymology 2`, ru `{{з|II}}`), `text` its etymology section |
| `form` | the form when the wiki makes one an entry of its own (pl `bić się` beside `bić`) |
| `group` | the wiki's own number of the entry, which sense ids refer to (pl `2` of `(2.1)`) |
| `headword` | the headword line or inflection template as written (`{{en-noun}}`, `{{bn-বিশেষ্য|…}}`) |
| `grammar` | gender, inflection class and the like as the wiki writes them (`rodzaj żeński`) |
| `pronunciation` | as of the language, when the wiki gives it per entry |
| `senses` | the definitions |
| `translations` | the translations |
| `relations` | `{"Synonyms": [...], "Hyponyms": [...], "Holonyms": [...]}`: kind relation of wiktsections.sqlite |
| `sections` | other sections of the entry as text: `{section: text}` (Usage notes, Inflection, Declension, Examples…) |
| `lexeme` | a Wikidata lexeme whose senses the wiki shows instead of its own (ar `{{معجمية|L1480479|…}}`) |
| `media` | files of Wikimedia Commons (pictures, sounds) |

## Sense

| field | meaning |
|---|---|
| `id` | `"1"`, `"1.2"` (nesting), or the wiki's own id (pl `"1.1"`, de `"1a"`) |
| `text` | the definition as wikitext |
| `examples` | examples and quotations of the sense |

## Translation

As up to v1.1.0, now always under its entry:

| field | meaning |
|---|---|
| `lang`, `lang_written` | the language of the translation: canonical code, as written |
| `term` | the translated word or phrase |
| `sense` | the sense id the wiki gives (pl `1.1`, de `[1]`, fr) |
| `gloss` | the gloss of the translation table (en `{{trans-top|domestic species}}`) |
| `gender`, `tr` | gender marks, transliteration |

The fields of the later passes (wiktlink: `page`, `home`, `en`,
`page_title`; wiktglos: `sense_ids`, `sense_method`, `sense_score`) keep their
meaning.

## How the wikis map onto it

| wiki layout | entries from |
|---|---|
| en: `Etymology N > Noun > Translations` | each part of speech of each etymology an entry, `etymology.n` = N |
| fr, es, it, nl, ja: part of speech headings or templates | each part of speech an entry |
| de: `Wasser ({{Sprache|Deutsch}})` per homograph, `{{Wortart|…}}` | each part of speech heading an entry; a second homograph heading: `etymology.n` 2 |
| pl: `{{znaczenia}}` with `''rzeczownik…''` lines, ids `(1.1)`, `(2.1)` | each `''…''` line a group: an entry, `group` 1, 2…; translations to the entry of their id's group |
| ru: `{{з|I}}` homographs, `Семантические свойства > Значение` | each homograph an entry (`etymology.n`), its part of speech from the morphology template |
| ar, ga, fy, hu (senses right under the language) | one entry, `pos` from the headword template when known, else `null` |
| kbd, nia, te (no part of speech) | one entry, `pos` from a template (kbd `{{псалъ-лъэпкъ|…}}`, nia `{{nom}}`) or `null` |
| translations at the language level (pl, ru, it, nl, zh) | to the entry of the sense id (pl `(1.1)` → group 1); one entry: to it; else to the entry they follow, else the first |

## What stays as it was

- Language markers, section names, codes: wiktlangs.sqlite, wiktsections.sqlite,
  `res/wiktionary/<wiki>.json`, unchanged.
- Wikitext of senses, examples and sections is kept as written; links are
  resolved later (wiktlink).
- `source` points into the dump: the original is always at hand.

## What changes

- No tree of the wiki's own headings: fixed levels language, entry, sense.
- Translations and relations are always under an entry.
- Sections that are no entries become `notes` of the language or `sections`
  of an entry, by name.
- The new format is written from the dump directly (a new branch), not from
  the JSONL of v1.1.0.

## Open questions

1. `sections` of an entry as text, or parsed further (inflection tables,
   examples) – text first, parsed per kind later.
2. The relation lists: terms only (`["kość", "szkielet"]`), or with sense ids
   where the wiki numbers them (pl `(1.1) [[kość]]`) – with sense ids.
3. Thesaurus pages (en `Thesaurus:water`): entries of their own, or relations
   of the main entry – entries of their own, `ns` 110.
