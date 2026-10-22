# defglos: definitions and translation glosses

Moved from `my_c/wikitools/defglos` (July 2025). Translation tables of
en.wiktionary have only a gloss (`{{trans-top|domestic species}}`); defglos
finds the definitions it belongs to.

- `src/defglos/Samples` reads `res/defglos/samples.txt`: 20 entries with
  definitions, glosses and expected links (` - hard` marks links that need
  knowledge rather than common words).
- `src/defglos/Similarity`: common runs of words weighted by rarity in the
  entry, as in the original `similarity.cpp`. Word equality can be replaced
  (the original used FastText cosine >= 0.5).
- `src/defglos/Matcher`: deterministic stages of `wstepny_pseudokod.txt`:
  order-preserving alignment, then the best definition for each gloss left.
  Linking subdefinitions to the gloss of their parent is optional; it lowers
  precision on the samples.
- `defglos` (tools/defglos.cpp) measures precision and recall on the samples;
  `--sweep` tries thresholds, `-v` lists wrong and missing links.

Not moved (stay in `my_c/wikitools/defglos`): FastText and SBERT/onnxruntime
experiments and the tokenizer server client (`rest.cpp`); embeddings are
planned through Ollama (`HANDOFF-3-definicje-glosy.md`). The diff
(`core/Diff`) compared the expected links, not similarities, so the
alignment is now a weighted dynamic programming over the similarity matrix.

Notes from the original project: `wstepny_pseudokod.txt`, `niejasności.txt`,
`todo.txt`, `ask_to_llm.txt`.
