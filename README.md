# wiktparse
Wiktionary parser

Dumps are read with [wikilib](../wikilib), built from `../wikilib`
(override with `-DWIKILIB_DIR=...`). The dump directory is set in
`~/.config/wikilib/config` or by the `WIKILIB_DUMP_DIR` environment variable:

```ini
dump_dir = ~/dump
```

## Tools

- `wiktneeded [output.jsonl]` – reads the Wikidata dump (`<dump_dir>/wikidata/`,
  also split into `...-multistreamN.xml-pApB.bz2` parts, with progress and ETA)
  and writes entities needed by wiktparse to `wiktNeeded.jsonl`: languages and
  language types, parts of speech and other Wiktionary section concepts (affixes,
  phrases, idioms, abbreviations, letters...), grammatical features, linguistics
  terms. Humans, publications, categories, names etc. are skipped. Selection
  rules are in `src/WikiData.cpp`. `wiktparse` only reads this file
  (`splitWikiDataResult()`).
- `wiktneeded --filter in.jsonl out.jsonl` – applies the current selection to an
  earlier result, without the dump.
