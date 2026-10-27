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
   └─ entries[]             one per part of speech (and group)
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

So an **entry** is a group of senses of one part of speech and of one group
when the wiki tells it (an etymology, a homograph, a number of its own). One
part of speech may come in several entries (de *Bank* the bench and *Bank* the
bank: two nouns; pl *bić* and its reflexive *bić się*, groups 1 and 2, the
latter with `pos_written` "czasownik zwrotny…", as the wiki has them).

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
| `etymology_chain` | the etymology as a text a program reads (see *Etymology chain*), of entries too |
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

As up to v1.1.0, now always under its entry, as an array sorted by `lang`
(those of no code last, by `lang_written`; the terms of one language in the
order of the wiki):

| field | meaning |
|---|---|
| `lang` | the language of the translation, a canonical code; `null` when not known |
| `lang_written` | the language as written, only when `lang` is `null` (names of languages in any language are for a reader to show, from wiktlangs.sqlite) |
| `term` | the translated word or phrase |
| `sense` | the sense id the wiki gives (pl `1.1`, de `[1]`, fr) |
| `gloss` | the gloss of the translation table (en `{{trans-top|domestic species}}`) |
| `gender`, `tr` | gender marks, transliteration |
| `image` | the term as a picture: pl hieroglyphs `[[Plik:Egyptian-mw.PNG\|link=mw]]` give `term` `mw`, `image` `Egyptian-mw.PNG`; a sign of a sign language without `link=` has the file as its term |

The fields of the later passes (wiktlink: `page`, `home`, `en`,
`page_title`; wiktglos: `sense_ids`, `sense_method`, `sense_score`) keep their
meaning.

## Etymology chain

Beside the etymology as written, `etymology_chain` gives it as a text a
program reads, one item a line, `n. relation language form`, with ` [m]` when
the item is of item m (a cognate of it, its descendant), else of the entry
(en *water*):

```
1. root ine-pro *wed-
2. from enm water
3. from ang wæter
4. from gmw-pro *watar
…
7. compare sco watter
```

Relations: `from` (inherited or derived), `borrowed`, `calque`, `compound`
(the form its parts by ` + `: `water + -y`), `root`, `compare` (a cognate),
`descendant` (of the item before it), `doublet`, `mention`, `attested`. The
items without ` [m]` of `from`, `borrowed`, `calque` are the chain, each of
the one before it. Each wiki says how it writes its etymologies in its
configuration (`etymology` of `res/wiktionary/<wiki>.json`): the relation of
a template and the parameters of its language and form, the words that give
the relation of a mention (en "from", "cognate with"; de "von", "vgl."), a
template of the whole chain (en `{{etymon|en|:inh|enm:red}}`), a template of
its abbreviations of languages (pl `Szablon:etym/język`: `niem` de, `ang` en),
the placeholders of no etymology (en `{{rfe}}`; `=…` the whole text). A way
shared by several wikis is a profile of `res/wiktionary/etymology_profiles.json`
(`en`: the templates of en.wikt, taken by si, bn, ja, zh, ms…), which a wiki
takes by `"etymology": {"profile": "en"}`. After the first paragraph with
items of the chain only cognates and descendants are read: the rest are
notes. An etymology of no chain, but a placeholder, is `etymology_unparsed`:
there is one, not read yet (prose, a wiki of no configuration).

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
   of the main entry – merged into the main entry as its relations, as
   translation subpages are merged (not done yet).
3. Translations belonging to several entries (en `====Translations====`
   under `===Etymology N===` beside its parts of speech, rare): to one entry,
   or to each – to one, as in the table.
