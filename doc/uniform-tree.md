# The uniform tree: one JSON shape for every Wiktionary

Polish version: `uniform-tree.pl.md`; change both together.

Draft of the format of branch `uniform-tree` (after v1.1.0). Every Wiktionary,
whatever its own layout, gives pages of the same shape, so that one program
reads all of them. This document describes the shape for people; the JSON
Schema follows it (`res/schema/uniform.schema.json`).

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
   └─ entries[]             one per part of speech (and group, and form)
      ├─ senses[]           the definitions, ids "1", "1.2" (nesting as ids, not levels)
      │  └─ examples[]      examples and quotations of the sense
      ├─ translations[]     of this entry, each to a sense id when the wiki says which
      └─ relations{}        Synonyms, Antonyms, Hypernyms, Derived terms…: lists of terms
```

Three levels only: **language**, **entry**, **sense**. The wiki's own
headings are not levels: what they say becomes attributes of an entry.

### Why the part of speech, not the etymology, below the language

- Every wiki gives a part of speech (a heading, a template, an italic line), or
  one can be told from an inflection template; where none can, the entry has
  `pos: null`. The level is always there.
- Only some wikis split by etymology or homograph (en `Etymology 1/2`, es
  `Etimología 1`, fr `{{S|nom|fr|num=2}}`, ru `{{з|I}}`, de two
  `{{Wortart|Substantiv|Deutsch}}` headings on *Bank*, each with its own
  `{{Herkunft}}`). As a level it would be empty or made up in most wikis. As
  attributes nothing is lost: en *rock* is eight entries (noun and verb of
  Etymology 1, verb and noun of 2, noun and verb of 3, a noun of 4 and of 5)
  with `group` 1, 1, 2, 2, 3, 3, 4, 5 and the text of their Etymology section
  as `etymology`.

So an **entry** is a group of senses of one part of speech, of one group when
the wiki tells it (an etymology, a homograph, a number of its own), of one
form when the wiki tells it (pl *bić* and *bić się* are two entries, both
verbs). One part of speech may come in several entries (de *Bank* the bench
and *Bank* the bank: two nouns).

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
| `pronunciation` | IPA, audio files, hyphenation of the whole language section, when the wiki gives them there (en `===Pronunciation===` before `===Etymology 1===`, pl `{{wymowa}}` lines without a sense id) |
| `audio` | sound files of the pronunciation: `["Pl-woda.ogg"]` |
| `etymology` | the etymology of the whole section, when the wiki gives it there and marks no part of it for an entry |
| `entries` | the entries |
| `notes` | sections of the whole language (en `===Anagrams===`, pl `{{źródła}}`, References, See also…): `{section: text}` |
| `media` | files of the language section marked for no sense or entry, as `media` of an entry |

## Entry

| field | meaning |
|---|---|
| `pos` | the part of speech, an English section name of wiktsections.sqlite (`Noun`, `Verb`, `Proper noun`, `Han character`); `null` when the wiki gives none |
| `pos_also` | further parts of speech of the same heading or line (de `{{Wortart|Substantiv|Deutsch}}, {{n}}, {{Wortart|Toponym|Deutsch}}`: `pos` `Noun`, `pos_also` `["Toponym"]`) |
| `pos_written` | the part of speech as written (`''rzeczownik, rodzaj żeński''`, `{{S|nom|fr|num=2}}`, `{{-ana-|mg}}`) |
| `group` | the wiki's own number of the entry or of its group of entries: en `Etymology 2`, es `Etimología 2`, fr `num=2`, ru `{{з|II}}` (the place of the homograph when the template has no number), pl `2` of `(2.1)` |
| `form` | the form when the wiki makes one an entry of its own (pl `bić się` beside `bić`) |
| `headword` | the headword line or inflection template as written (`{{en-noun}}`, `{{bn-বিশেষ্য|…}}`) |
| `grammar` | gender, inflection class and the like as the wiki writes them (`rodzaj żeński`, de `{{f}}, ''Bänke''`) |
| `etymology` | the etymology of this entry as text: its own section (de `{{Herkunft}}` under the part of speech), that of its group (en `Etymology 2`, a ru homograph), or the lines of the language's etymology marked for it (pl `(2.1)`, fr `{{laé|fr|nom|2}}`); none: that of the language |
| `pronunciation` | as of the language, when the wiki gives it for the entry or its group (de `{{Aussprache}}`, en `====Pronunciation====` under `===Etymology 2===`, pl `{{wymowa}}` lines with ids); none: that of the language |
| `audio` | as of the language, of the pronunciation of the entry |
| `senses` | the definitions |
| `translations` | the translations |
| `see` | translations on other pages (en `{{trans-see|…}}`): `{"title", "gloss"}` |
| `subpages` | translation subpages merged into the translations (en `water/translations`): `{"title", "source"}` |
| `relations` | `{"Synonyms": [{"term": "warownia", "sense": "1.1"}, …], …}`: sections of kind relation of wiktsections.sqlite and relation templates of senses (en `#: {{syn|en|…}}`); `sense` the sense id as the wiki gives it (pl `(1.1)`, de `[1, 6]`, ru the place of the `#` line, en the sense the template stands under), `qualifier` a label of the line (pl `{{zdrobn}}`, ru `прилагательные=`) |
| `sections` | other sections of the entry as text: `{section: text}` (Usage notes, Inflection, Descendants…) |
| `lexeme` | the Wikidata lexeme of the entry (ru `{{Лексема в Викиданных|L110019}}`); ar `{{معجمية|L1480479|…}}` shows its senses instead of the wiki's own |
| `media` | pictures of Wikimedia Commons (sounds are in `audio`): `{"file": "…", "caption": "…", "sense": "1.1"}` (pl `[[Plik:…|thumb|zamek (1.1)]]`, ru `{{илл|…|Замок [1]}}`) |

## Sense

| field | meaning |
|---|---|
| `id` | `"1"`, `"1.2"` (nesting), or the wiki's own id (pl `"1.1"`, de `"1a"`) |
| `text` | the definition as wikitext, without the examples taken out of it |
| `examples` | `[{"text": "…", "source": "…"}]`: examples and quotations of the sense, from its lines (en `#:`, fr `#*`, ru `{{пример|…}}`) or from an examples section by sense id (pl `{{przykłady}}` `(1.2)`, de `{{Beispiele}}` `[1]`); `source` the author, work and year of a quotation when given |

## Translation

As up to v1.1.0, now always under its entry:

| field | meaning |
|---|---|
| `lang`, `lang_written` | the language of the translation: canonical code, as written |
| `term` | the translated word or phrase |
| `sense` | the sense id the wiki gives (pl `1.1`, de `[1]`, fr) |
| `gloss` | the gloss of the translation table (en `{{trans-top|domestic species}}`) |
| `gender`, `tr` | gender marks, transliteration |
| `image` | the term as a picture: pl hieroglyphs `[[Plik:Egyptian-mw.PNG\|link=mw]]` give `term` `mw`, `image` `Egyptian-mw.PNG`; a sign of a sign language without `link=` has the file as its term |

The fields of the later passes (wiktlink: `page`, `home`, `en`,
`page_title`; wiktglos: `sense_ids`, `sense_method`, `sense_score`) keep their
meaning.

## Where things go

Every part of a language section goes to what it belongs to:

1. By the headings: what stands under a part of speech belongs to its entry;
   what stands under an en `Etymology N` or a ru homograph beside its parts of
   speech belongs to all of their entries.
2. By marks in the lines: sense ids (pl `(2.1)`, de `[1]`) to the entry of the
   sense; entry marks (fr `{{laé|fr|nom|2}}`) to that entry; ru relation lists
   by place: the n-th `#` line to sense n (`?` and `—` for none).
3. What belongs to the whole language stays there: `pronunciation`,
   `etymology`, `notes`.

What belongs to several entries: pronunciation and etymology are copied to
each of them, relations too (en Derived terms of `Etymology 2` beside its noun
and verb; pl `{{pokrewne}}` lines without an id); translations go to one entry
(the last row of the table below); other sections become `notes` of the
language.

## How the wikis map onto it

| wiki layout | entries from |
|---|---|
| en: `Etymology N > Noun > Translations` | each part of speech of each etymology an entry, `group` N |
| fr, es, it, nl, ja: part of speech headings or templates | each part of speech an entry; fr `num=2`, es `Etimología 2`: `group` 2 |
| de: `Bank ({{Sprache|Deutsch}})`, a `{{Wortart|…}}` heading per part of speech and per homograph | each `{{Wortart}}` heading an entry, with its own `{{Herkunft}}` and `{{Aussprache}}`; its sections by the ids `[1]` of their lines |
| pl: `{{znaczenia}}` with `''rzeczownik…''` lines, ids `(1.1)`, `(2.1)` | each `''…''` line an entry, `group` 1, 2…; every section of the language (`{{wymowa}}`, `{{przykłady}}`, `{{synonimy}}`, `{{etymologia}}`, `{{tłumaczenia}}`…) split by the ids of its lines |
| ru: `{{з|I}}` homographs, `Семантические свойства > Значение` | each homograph an entry (`group`), its part of speech from the morphology template, its sections under it |
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
- Translations and relations are always under an entry, examples under their
  sense.
- Sections go where they belong (see *Where things go*), not by their name.
- The new format is written from the dump directly (a new branch), not from
  the JSONL of v1.1.0.

## Open questions

1. `sections` of an entry as text, or parsed further (inflection tables) –
   text first, parsed per kind later.
2. Thesaurus pages (en `Thesaurus:water`): entries of their own, or relations
   of the main entry – entries of their own, `ns` 110.
3. Translations belonging to several entries (en `====Translations====`
   under `===Etymology N===` beside its parts of speech, rare): to one entry,
   or to each – to one, as in the table.
