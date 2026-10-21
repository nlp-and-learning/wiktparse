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
  organizations...); they are logged in `wiktNeeded_terms_n.tsv`.
- Ids of classes and properties used by `wiktneeded` and `splitWikiDataResult()`
  are in `res/wikidata_selection.tsv` (`--rules FILE` for another file); after
  changing it, `wiktneeded --filter` reapplies the rules to an earlier result
  without reading the dump (rules adding new entities need a dump pass).
