# wiktparse
Wiktionary parser

Dumps are read with [wikilib](../wikilib), built from `../wikilib`
(override with `-DWIKILIB_DIR=...`). The dump directory is set in
`~/.config/wikilib/config` or by the `WIKILIB_DUMP_DIR` environment variable:

```ini
dump_dir = ~/dump
```

## Tools

- `wiktneeded [-j N] [output.jsonl]` – reads the Wikidata dump
  (`<dump_dir>/wikidata/`, split into `...-multistreamN.xml-pApB.bz2` parts) in
  N threads (default 8, each takes the next part), with progress and ETA.
  Thread n writes `wiktNeeded_n.jsonl`; old `wiktNeeded_*.jsonl` are removed
  first. Selected entities: languages and
  language types, parts of speech and other Wiktionary section concepts (affixes,
  phrases, idioms, abbreviations, letters...), grammatical features, linguistics
  terms. Humans, publications, categories, names etc. are skipped. Selection
  rules are in `src/WikiData.cpp`. `wiktparse` only reads these files
  (`splitWikiDataResult()`; falls back to a single `wiktNeeded.jsonl`).
- `wiktneeded --filter in.jsonl out.jsonl` – applies the current selection to an
  earlier result, without the dump.
- `wiktheaders [-j N] [output_dir] [lang...]` – collects section headers
  (`==header==`, and lines with one template only) of all Wiktionaries per
  namespace and level. A level is trusted when its headers repeat (80% of
  occurrences on >= 3 pages); the entry word is removed from headers like
  `kot ({{język polski}})`. Writes `report.tsv`, `<lang>.tsv` and `vocab.tsv`
  (texts, template names and arguments of trusted levels).
- `wiktterms [headers_dir] [terms_dir] [--ns wiki:Namespace]` – from
  `wiktheaders` output (no dumps) writes `terms/<wiki>.txt`, one term per line:
  language names, parts of speech, section names. Headers written as templates
  give section names without language codes (`{{S|verbe|fr}}` -> `verbe`).
  Main namespace by default.
- `wiktneeded --terms terms` (default when `./terms` exists) also selects
  entities whose label or alias in the language of a Wiktionary equals one of
  its terms (case-insensitive) in at least `--min-term-languages N` (default 2)
  Wiktionaries, with stricter exclusions (works, places with P625/P131,
  organizations...); they are logged in `wiktNeeded_terms_n.tsv`. A final
  pass over the outputs removes homonyms: entities whose terms are already
  labels of claims-selected entities ("root" of a plant besides the linguistic
  root); `--keep-homonyms` keeps them.
- `wiktneeded --items FILE` always selects the listed Q ids (default
  `./wiktlangs_qid.txt` when present); redirect pages of merged items are skipped.
- `wiktlangs [--markers FILE] [--module-names FILE] [output.sqlite] [wikidata.jsonl...]` –
  languages into SQLite (layout: `doc/wiktlangs-schema.md`): `language`, one
  row per language (canonical code, English name, ISO 639-1/3, Glottolog,
  Wikimedia code, Q id, family...), `language_name`, the name of a language in
  another language (Wikidata labels and aliases, en.wikt names, Lua modules and
  language headings of Wiktionaries, configuration aliases), and `code_map`,
  codes of other schemes to canonical codes. Sources: English Wiktionary
  (`Module:languages/data...`, etymology languages, families, scripts, read
  through the index only), Wikidata (`wiktNeeded_*.jsonl`), the SIL tables in
  `res/sil`, Glottolog (`res/glottolog/languoid.csv`, Glottolog 5.3, CC BY 4.0,
  see `res/glottolog/README.txt`), `res/extra_languages.tsv`, `wikt2jsonl`
  markers and `wiktmodules` names. Languages en.wikt does not have, or merges
  into another one, are added too: ISO 639-3 codes of language modules of other
  Wiktionaries (`module_other.tsv`, kind `wiktionary`: fr `aao` arabe saharien)
  and of English Wikipedia infoboxes (kind `wikipedia`: Saidi Arabic `aec`).
  Also writes `wiktlangs_qid.txt` (with new ids
  of merged items) and `wiktlangs_missing_qid.txt`. `--fetch-missing` fetches the
  missing entities from the Wikidata API into `wiktlangs_fetched.jsonl`, which
  is read on every later run. With `./infoboxes.jsonl` (from `wiktparse --infoboxes`, run it
  first), languages of English Wikipedia infoboxes which are not known yet and
  their unknown varieties (lcN/ldN) are added with kind `wikipedia`.
- `wiktmodules [-j N] [-o modules.tsv] [--names module_names.tsv] [--other module_other.tsv] [wiki...]` –
  Lua modules of all Wiktionaries that list languages (`l['aa'] = { name = 'afar' }`,
  `["阿歐語"] = "aou"`): `module_names.tsv` names of en.wikt codes,
  `module_other.tsv` codes unknown to en.wikt, both read by `wiktlangs`.
- `wiktsamples [-j N] [-n pages] [-o samples] [--db wiktlangs.sqlite] [wiki...]` – writes
  `samples/<wiki>/`: raw wikitext of 5 main namespace pages of every
  Wiktionary (the words for water, dog, house, red and go, taken from English
  Wiktionary translation tables; otherwise pages with the most headings) and
  one page of another namespace, with `meta.json`; see `samples/README.md`.
  `--other N` adds N pages of other namespaces (thesaurus, reconstructions,
  appendices, in turn). `-n 10 --other 5 -o samples10` is for reading by hand;
  `-n 100 -o samples100` writes 100 main namespace pages per wiki spread over
  the index (about 15 thousand pages, 66 MB) for testing converters without
  the dumps. Run it in the build directory; samples are not versioned.
  Reads only the needed indexed ranges (about 20 s for all 198 dumps).
- `wikt2jsonl [-i samples100] [-o jsonl] [wiki...]` – converts pages (by default
  the samples) to JSONL with the configuration of `res/wiktionary`: language
  sections with codes, headings, senses, translations; `jsonl/stats.tsv` shows
  per wiki how much was recognized. One page is one line: language sections
  with canonical codes, each with the tree of its sections as the headings
  give it (template lines like pl `{{synonimy}}` one level below), every node
  under its English name with senses or translations where they stand (not
  linked yet). HTML entities are decoded in the output (`&#171;@&#187;` «@»,
  `&nbsp;`, `&gt;`, names of `res/html_entities.tsv` beyond those of
  wikilib), but those of the markup of a template or a link inside it
  (`{{t|en|a&#124;b}}`), so that its wikitext reads as before; the pages are
  parsed with the entities of markup characters as written. Schema: `res/schema/page.schema.json`;
  `tools/validate_jsonl.py jsonl` checks the output. `--uniform` writes the
  pages in the uniform tree instead (`doc/uniform-tree.md`, schema
  `res/schema/uniform.schema.json`): languages, entries by part of speech
  (with the group, etymology and pronunciation they belong to), senses with
  their examples, translations and relations (with sense ids) under their entry.
- `wiktbrowse [file.jsonl | file.jsonl.bz2]` – browser of JSONL files (wikt2jsonl
  output, also any JSONL such as wiktNeeded), a separate project in `browser/`
  (Qt6 and qcodeedit: the sibling `../qcodeedit` when present, otherwise
  fetched), so that the rest needs no Qt:
  `cmake -S browser -B browser/build -DCMAKE_BUILD_TYPE=Release && cmake --build browser/build`.
  Lines with titles (or Wikidata id and English label), a filter, the selected
  line as indented JSON in a read-only editor with Kate highlighting and
  folding, and as a tree. `.jsonl.bz2` files of `wikt2jsonl --bz2` are read
  through their `.jsonl.idx`, decompressing only the block shown. F4 shows
  Cyrillic in Latin letters, its sound the Polish way (вода woda, жёлтый
  żołtyj, семья siem'ja; letters of Ukrainian, Serbian, Kazakh… roughly), in
  all tabs ("(lat)" on them), again: as written. F6 shows links as their text
  and templates of one unnamed parameter as it (`[[вода|воде]]` воде,
  `{{выдел|водата}}` водата; not `{{t|en|water}}`, files, categories) and HTML
  entities as their characters (`&#171;@&#187;` «@», `&#124;` |) in all tabs
  ("(text)" on them), again: as written.
  View > Original shows the wikitext of the page from the dump (Settings >
  Dump directory) at its `source`; when the page there has another title (a
  dump of another date), or with Settings > Find pages by title, the page is
  looked up by its title in the dump's `-index.txt.bz2` (read once per dump).
  WikiTree, with Original, shows that wikitext as a tree by the depth of its
  headings (`==English==` > `===Noun===` > `====Translations====`), the other
  lines under their heading. F5 switches Original and WikiTree to OriginalT
  and back: headings and section template lines translated, language names
  ("Polski", "język angielski", {{język polski}}) to codes (wiktlangs.sqlite)
  and sections (=== Rzeczownik ===, {{znaczenia}}, {{S|nom|fr}}) to English ones
  (wiktsections.sqlite), or with Settings > Translation language to names in
  that language, falling back to the code and the English section; codes
  written in templates ({{langue|fr}}) stay. The databases are found next to
  the opened file or up to two directories above it. The status line shows where the cursor is: the
  JSON path in JSON (`$.sections[0].sections[3].translations[5]`), the headings
  in Original, the nodes from the root in Tree and WikiTree. Alt+1..Alt+4 switch
  the tabs; between JSON and Tree, and between Original and WikiTree, the other
  tab shows the same place (the tree expanded to it).
- `tools/unknownheadings.py [-d .] [-o unknown_headings.tsv] [--min-pages 4] [wiki...]` – the
  headings of the dumps (`headers/<wiki>.tsv`) known neither as a language
  (`wiktlangs.sqlite`) nor as a section (`wiktsections.sqlite`), by pages, most
  first, as the parser keys them: to name them in
  `res/wiktionary_section_names.tsv` or in the wiki's configuration. Headings
  of fewer than `--min-pages` pages (by default 4: those of 1 to 3 pages are
  mostly words written as headings) are left out; a column `kind` guesses
  language or section by the level of the wiki's language headings.
- `tools/etymologytemplates.py [-i uniform1000] [-n 15] [--examples 3] [--all] wiki...` –
  the templates of the etymologies of a wiki not read yet (`etymology_unparsed`
  of `wikt2jsonl --uniform`), most first, with examples of their parameters,
  marked when the wiki's configuration has them, and the words between them:
  to write the `etymology` configuration of the wiki.
- `tools/jsonlpick.py [-s samples100] [-i jsonl_senses] [-o jsonl100] [wiki...]` –
  the pages of a sample taken from a block output (`wikt2jsonl --dump`,
  `wiktlink`, `wiktglos`) into plain `<wiki>.jsonl` in the order of the sample,
  for the browser (needs `zstandard`). `/translations` subpages are already
  merged into their pages, so they are not found on their own.
- `wordnetsample [-i jsonl_senses] [-o jsonl_wordnet] [--homonyms res/wordnet/homonyms.json] [--base res/wordnet/base.json] [--high 300] [--medium 300] [--other-high 100] [--other-medium 100] [--min 200] [-j 16]` –
  a sample for the wordnet, closed over translations: in the seed languages
  (en and those of `--homonyms` and `--base`: pl, fr) their homonyms, base
  words (water, dog, house… in each language) and native entries of
  many and of a medium number of translations (`--high`/`--medium` in en,
  `--other-*` in the others), the pages their translations point to (`home`;
  `page` in the seed wiki, as many as its chosen ones), native entries of every
  wiki translating into one of them, own entries of many translations
  for a wiki with fewer than `--min` pages, and non-native entries of the chosen
  ones in other wikis (pl.wikt `chien` beside fr.wikt `chien`) as many as the
  native ones of a wiki. Plain `<wiki>.jsonl` and `selection.tsv` (the reason
  of every page); some 15 s for all wikis.
- `defglos [samples.txt] [-v] [--sweep]` – links English definitions to
  translation glosses on `res/defglos/samples.txt` and reports precision and
  recall against the expected links; see `doc/defglos/README.md`.
- Ids of classes and properties used by `wiktneeded` and `splitWikiDataResult()`
  are in `res/wikidata_selection.tsv` (`--rules FILE` for another file); after
  changing it, `wiktneeded --filter` reapplies the rules to an earlier result
  without reading the dump (rules adding new entities need a dump pass).
