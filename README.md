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
- `wiktlangs [output.sqlite] [wikidata.jsonl...]` – languages, etymology
  languages (varieties, historical stages), families and scripts of English
  Wiktionary (`Module:languages/data...`, read through the index only) into
  SQLite, with their names in many languages from Wikidata labels and aliases
  (`wiktNeeded_*.jsonl`). Also writes `wiktlangs_qid.txt` (with new ids of
  merged items) and `wiktlangs_missing_qid.txt`. `--fetch-missing` fetches the
  missing entities from the Wikidata API into `wiktlangs_fetched.jsonl`, which
  is read on every later run. Table `wd_code_map` maps codes of Wikidata
  languages (ISO 639-1/2/3, Wikimedia, Glottolog) to en.wikt codes, using the
  SIL tables in `res/sil`. With `./infoboxes.jsonl` (from `wiktparse`, run it
  first), languages of English Wikipedia infoboxes which are not known yet (no
  code maps to en.wikt, name not an en.wikt name or alias) and their unknown
  varieties (lcN/ldN) are added to `lang` with kind `wikipedia` (type
  `infobox`, `infobox family` or `infobox variety`). Table `glottolog` has the
  languoids of `res/glottolog/languoid.csv` (Glottolog 5.3, CC BY 4.0, see
  `res/glottolog/README.txt` for citation); Glottolog codes of Wikidata
  missing there are noted in `wd_code_map`.
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
  per wiki how much was recognized. First version.
- `defglos [samples.txt] [-v] [--sweep]` – links English definitions to
  translation glosses on `res/defglos/samples.txt` and reports precision and
  recall against the expected links; see `doc/defglos/README.md`.
- Ids of classes and properties used by `wiktneeded` and `splitWikiDataResult()`
  are in `res/wikidata_selection.tsv` (`--rules FILE` for another file); after
  changing it, `wiktneeded --filter` reapplies the rules to an earlier result
  without reading the dump (rules adding new entities need a dump pass).
