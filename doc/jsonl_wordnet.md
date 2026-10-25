# jsonl_wordnet: a sample of parsed Wiktionaries for a multilingual wordnet

Pages of 177 Wiktionaries parsed into JSON by
[wiktparse](https://github.com/nlp-and-learning/wiktparse.git): language
sections, parts of speech, definitions with examples, translations with the
definition they belong to, and links of every translation to the pages of its
term in other Wiktionaries. The sample is closed over translations, so that
both ends of a translation are in it, and is meant for building a wordnet of
synsets linked across languages.

## Source and license

- Source: the Wiktionary dumps `<wiki>wiktionary-20261001-pages-articles-multistream.xml.bz2`
  of [dumps.wikimedia.org](https://dumps.wikimedia.org/), 2026-10-01.
- Content: Wiktionary contributors, under the
  [Creative Commons Attribution-ShareAlike 4.0 License](https://creativecommons.org/licenses/by-sa/4.0/).
  This sample is a derivative work under the same license: keep the attribution
  (the Wiktionary of each page, its `wiki` and `title`) and share changes alike.
- Made with wiktparse: `wikt2jsonl` → `wiktlink` → `wiktglos` → `wordnetsample`;
  code and documentation at https://github.com/nlp-and-learning/wiktparse.git.

## Files

- `<wiki>.jsonl`: one page per line (UTF-8 JSON), `wiki` the Wiktionary code
  (`en`, `pl`, `fr`…).
- `selection.tsv`: `wiki`, `title`, `reason`: why each page is in the sample.

## How the sample was chosen

1. en.wikt: native English entries, 300 of many translations (at random of the
   3000 with most) and 300 of 10–40 translations (reason `high`, `medium`).
2. The pages their translations point to: the term's page in the Wiktionary of
   its language (`translation of en water` for pl `woda`), and as many en.wikt
   pages of the terms (Polish `pies` in en.wikt: `pl translation of en dog`).
3. Native entries of every Wiktionary with a translation into one of the chosen
   en entries (`translates into a chosen en entry`).
4. A Wiktionary with fewer than 200 pages: its own native entries of many
   translations (`filling: N terms`).
5. Non-native entries of the chosen ones in other Wiktionaries, as many as the
   native ones (pl.wikt `chien`, its French section, beside fr.wikt `chien`:
   `non-native fr beside the fr wiki`).

## Fields

The full schema: `res/schema/page.schema.json` of the repository. In short:

- page: `wiki`, `title`, `ns` (namespace), `source` (bz2 stream offset and page
  number in it, in the dump of 2026-10-01), `sections`;
- language section: `code` (ISO 639 / Wiktionary code), `language` (as written
  in the wiki), `native` (the language of the wiki), `sections`;
- section: `section` (English name: `Noun`, `Etymology`, `Translations`…),
  `heading`, `senses`, `translations`, `children` (sections under a part of
  speech), `text` (wikitext not parsed further), `media`; `subpage`,
  `source`, `source_title` of a merged `X/translations` subpage;
- sense: `id` (`1`, `1.2`; pl `1.1`, de `1a`), `text` (wikitext), `examples`;
- translation:
  - `lang`, `lang_written`, `term`, `gender`, `tr` (transliteration);
  - `sense`: the sense number given by the wiki (pl `(1.1)`, de `[1]`, fr, es);
  - `gloss`: the gloss of the translation table (en `{{trans-top|domestic species}}`);
  - `sense_ids`: the definitions the gloss is linked to, with `sense_method`
    (`positional`, `best`, `single`, `number` by rules; `embedding`; `llm`;
    `vote`, two of three LLMs; `manual`; `minority`, one LLM of three, the
    weakest) and `sense_score`;
  - `page`: this Wiktionary has a page of the term with a section of its
    language; `home`: the Wiktionary of the language has one; `en`: en.wikt has
    one; `page_title`: the title found when it is not the term.

## Reading

```python
import json
for line in open('pl.jsonl', encoding='utf-8'):
    page = json.loads(line)
    for section in page['sections']:
        print(page['title'], section['code'], section['native'])
```

`wiktbrowse` of the repository opens the files (`browser/`).
