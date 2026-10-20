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
