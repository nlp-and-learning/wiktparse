# How Wiktionaries differ (for the per-wiki configuration)

Analysis of `samples/` (wiktsamples, dumps 2026-10-01): the pages for water,
dog, house, red and go in en, pl, fr, de, ru, es, it, nl, ja, zh, and a
feature scan of all 198 wikis. Every difference below becomes a field of
`res/wiktionary/<wiki>.json`; wikis of one profile share defaults.

## 1. The ten large wikis

| wiki | language of a section | homographs / etymologies | part of speech | sections | senses | translations | sense of a translation |
|---|---|---|---|---|---|---|---|
| en | `==English==` (L2, name) | `===Etymology 1===` (L3), POS one level lower | heading `====Noun====` | headings `=====Synonyms=====` | `#`, `##`; examples `#:` `#*` | `{{trans-top|gloss}}` + `* Polish: {{t+|pl|woda}}` (`t`, `t+`, `tt`, `tt+`, in `{{multitrans|data=…}}`), often on `X/translations` | gloss only (sometimes `id=Q283`) |
| pl | `== woda ({{język polski}}) ==` (L2, word + template with the language name) | – (one section per language) | italic line after `{{znaczenia}}`: `''rzeczownik, rodzaj żeński''` | template lines `{{znaczenia}}`, `{{synonimy}}`, `{{tłumaczenia}}` | `: (1.1)` (POS number . sense number) | `* angielski: (1.1) [[water]]` (language name, links) | **explicit** `(1.1)` |
| fr | `== {{langue|fr}} ==` (L2, code) | `{{S|nom|fr|num=2}}` | heading `=== {{S|nom|fr}} ===` | headings `==== {{S|synonymes}} ====` | `#`, `##`; examples `#* {{exemple|…}}` | `{{trad-début|gloss|1}}` + `* {{T|en}} : {{trad+|en|water}}` | **explicit** number in `trad-début` |
| de | `== Wasser ({{Sprache|Deutsch}}) ==` (L2, word + language name) | one L2 per homograph | heading `=== {{Wortart|Substantiv|Deutsch}}, {{n}} ===` | template lines `{{Bedeutungen}}`, `{{Synonyme}}`; `==== {{Übersetzungen}} ====` | `:[1]`, `:[1a]` | `{{Ü-Tabelle|1|G=gloss|Ü-Liste=*{{en}}: {{Ü|en|water}}…}}` | **explicit** `Ü-Tabelle|1` |
| ru | `= {{-ru-}} =` (**L1**, code) | `== {{з|ударение=вода́}} ==` (L2) | template in `=== Морфологические … ===`: `{{сущ-ru|…}}` | headings `==== Значение ====`, `==== Синонимы ====` | `#` | `{{перев-блок|gloss|en={{t|en|water}}|…}}` (code parameters) | gloss only |
| es | `== {{lengua|es}} ==` | `=== Etimología 1 ===` | heading `==== {{sustantivo femenino|es}} ====` | headings `==== Traducciones ====` | `;1 {{csem|…}}: text` | `{{trad-arriba}}` + `{{t|en|a1=1|t1=water}}` | **explicit** `a1=1` |
| it | `== {{-it-}} ==` (L2, code) | – | template line `{{-sost-|it}}` | template lines `{{-sin-}}`, `{{-trad-}}` | `#` | `{{Trad1|gloss}}` + `:*{{en}}: [[water]]` | gloss only |
| nl | line `{{=nld=}}` (no heading) | `{{-noun-|nld}}` repeated | template line `{{-noun-|nld}}` (after `{{-nlnoun-|…}}`) | template lines `{{-syn-}}`, `{{-trans-}}` | `#`, `##` | `{{trans-top|gloss}}` + `*{{deu}}: {{trad|de|…}}`; often `water/vertalingen` | gloss only |
| ja | `=={{L|ja}}==` (L2, code); `==漢字==` on kanji pages | – | heading `==={{noun}}===` | headings `===={{prov}}====` | `#` | `{{trans-top|gloss}}` + `*{{T|en}}: {{t|en|protection}}` (from the dump; samples have none) | gloss only |
| zh | `==漢語==` (L2, name) | – | heading `===名詞===` | headings `====釋義====`, `===翻译===` | `#` | `{{翻譯-頂}}` + `*英语：[[water]]` (name, full-width colon) | gloss only |

Observations:
- Four wikis number senses in translations (pl, fr, de, es): their
  definition-translation links are deterministic; HANDOFF-3 (glosses) is
  needed for en, ru, it, nl, ja, zh and most small wikis.
- Language of a section is a **name** (en, zh, pl in a template, de in a
  template) or a **code** (fr, es, it, ru, ja, nl); names map to codes through
  `wiktlangs.sqlite` (`lang_name`, labels in the wiki's language).
- Part of speech is a heading text (en, zh), a heading template (fr, es, ja,
  de), a template line (it, nl), an italic line (pl) or an inflection template
  (ru): the POS rule is per wiki.
- Sections are headings (en, fr, es, ru, ja, zh) or template lines (pl, de,
  it, nl); one wiki may mix them (de: `==== {{Übersetzungen}} ====`).
- Sense ids: implicit order of `#` (most), explicit `(1.1)`, `[1]`, `;1`.
  Nesting: `##` (en, fr, nl), `1.1` (pl: POS.sense), `[1a]` (de).
- Japanese and Chinese entries for words also exist as character pages
  (`==漢字==`); ja main entries are often in kana (`いぬ`), kanji titles are
  character pages. Samples for ja/zh should add a kana/word page.
- Translation subpages: en `X/translations`, nl `X/vertalingen`.

## 2. All 198 wikis by language marker

From the feature scan (the most frequent marker of the sample pages):

| profile | marker | wikis |
|---|---|---|
| A. heading with the language name | `==English==` | am an ang av bcl blk chr ckb cs dv en fa fi gn gom gu guw gv ha he hi hif hsb hy ig iu ja ka kcg kk kl km kn ko ks kw li lo mk ml mn mni mr ms my nds nn no or pa pnb ps sat sd sh shn si simple sk sq sr ss st sv ta tcy te th tl tn tr ts tt ur wo zh zh_min_nan zu (78) |
| A1. heading, name at level 1 | `= नेपाली =` | ne sa skr |
| B. heading with a code template | `== {{langue|fr}} ==` and the same with another template name: `lengua` es, `limba` ro, `Lingvo` eo, `bahasa` id min, `basa` bew bjn jv, `ziman` ku, `zıwan` diq, `L` wa, `ⵜⵓⵜⵍⴰⵢⵜ` zgh, `تىلى` ug; `-lingua-` la | fr shy yue es ro eo id min bew bjn jv ku diq wa zgh ug la |
| B1. heading with `{{-xx-}}` | `== {{-it-}} ==` | it ca el kaa lmo |
| B2. heading with a language template without code | `== {{Dil…}} ==`, `{{اللغة…}}`, `{{sunda…}}` | az tk ar su sw ln io lt btm gor mad rw jbo mnw lb mg |
| C. level 1 heading, code | `= {{-ru-}} =` | ru pt ast be uz; also `= {{en…}} =` as, `= {{Y|xx}} =` sg |
| D. heading with the word and the language | `== woda ({{język polski}}) ==` | pl de hr csb |
| E. template line, no heading | `{{-xx-}}` or `{{=xx=}}` on its own line | `{{-xx-}}`: ay bg co et eu fj fo fy ga gl hu ia ie ik is ky lv mt na nah om qu roa_rup scn sm so tg ti tpi vec vi vo za; `{{=xx=}}`: af br cy da gd mi nl oc sl uk |
| F. too small or no entries in samples | – | aa ab ak bh bi bm bn bo bs ch cr dz kbd mh nia pi rm rn sc sn to tw xh yi yo |

Translation templates found in samples (most frequent per wiki): `t` (da eo
es ga gl hu ia id ky mni mt sw tr uk vec vi zu), `t+` (ckb fy kcg la lt wa),
`trad` (ast ca fr gn lv oc ro vo), `trans-top` (lo nl sl uz), `trans` (an
ks), plain `* name: [[x]]` (pl and 8 others), `*{{xx}}: [[x]]` (it and 5
others); 139 wikis had no translation tables in their samples. The same
template name means different things per wiki (`{{t|en|a1=1|t1=water}}` in
es, `{{t|pl|woda}}` in en).

## 3. Configuration fields

A profile gives defaults; a wiki file overrides them. Draft
(`res/wiktionary/<wiki>.json`, JSON Schema in `res/schema/`):

```jsonc
{
  "wiki": "pl",
  "profile": "D",
  "language": {
    "heading_level": 2,
    "form": "word-and-template",            // name | code-template | dash-template | word-and-template | line-template
    "template": "język {name}",            // how the language is read
    "value": "name"                        // name -> code through wiktlangs.sqlite, or code
  },
  "homographs": {"heading": null, "numbered": "Etymology {n}"},
  "sections": {
    "form": "template-line",              // heading | template-line | mixed
    "names": "res/wiktionary_section_names.tsv"   // local name -> English (HANDOFF-2 step 3)
  },
  "pos": {"form": "italic-line", "after_section": "znaczenia"},   // heading | heading-template | template-line | italic-line | inflection-template
  "senses": {"marker": ": ({pos}.{n})", "nesting": "id"},         // "#" with "##" nesting, ":[{n}]", ";{n}"
  "examples": {"section": "przykłady"},
  "translations": {
    "section": "tłumaczenia",
    "table": null,                         // {"top": "trans-top", "bottom": "trans-bottom", "gloss": 1}
    "item": "* {language-name}: ({sense}) [[{term}]]",
    "term_templates": [],                  // e.g. {"t": {"lang": 1, "term": 2}}, es {"t": {"lang": 1, "term": "t{i}", "sense": "a{i}"}}
    "sense_ids": true,
    "subpage": null                        // "{title}/translations" (en), "{title}/vertalingen" (nl)
  },
  "namespaces": {"thesaurus": null, "reconstruction": null, "appendix": 100}
}
```

Namespaces come from the dump `<siteinfo>` (`DumpReader::read_site_info()`);
the config only says which of them hold entries.

## 4. Order

1. Write the configs of en, pl, fr, de, ru, es, it, nl, ja, zh by hand from
   the table above and test them on the samples.
2. Profiles A, B, B1, C, E as defaults with their template names, the small
   wikis assigned by the scan (section 2) and corrected where samples show
   other forms.
3. Profile B2 and D wikis one by one (few, irregular).
