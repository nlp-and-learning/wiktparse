#!/usr/bin/env python3
"""Glosses of translation tables linked to definitions by embeddings (HANDOFF-3, stage 3).

Input: the output of wiktglos (<dir>/<wiki>.jsonl.zst with its .dict), whose deterministic
links stay. Each entry (a part of speech with senses and translation tables) is matched
within itself: the cosine of every gloss with every definition of the entry, the best
definition when it is ahead of the second by a margin.

  glossembed.py --eval  [-i jsonl_senses] [--wiki en] [--model qwen3-embedding:4b] [--entries 3000]
      the confident deterministic links (positional, score >= 0.3) predicted again, their
      accuracy by margin: for choosing the model and the thresholds
  glossembed.py --apply [-i jsonl_senses] [--wiki en] [--model ...] [-o glossembed-en.tsv]
      the glosses without a link: wiki, title, language, path of the node, gloss, sense id,
      score, margin; for wiktglos --extra

Embeddings by sentence-transformers on the GPU (--backend st; the venv with torch and
sentence-transformers: ~/.venvs/embed/bin/python glossembed.py ...), else by Ollama
(/api/embed; --backend ollama); auto: the first that works. Cached in glossembed-<model>.npz.
"""
import argparse
import hashlib
import json
import os
import random
import re
import subprocess
import sys
import urllib.request

import numpy as np

QUERY = 'Instruct: Given a short gloss of a word sense, retrieve the dictionary definition of that sense\nQuery: '


def pages(directory, wiki):
    base = os.path.join(directory, wiki + '.jsonl')
    command = ['zstdcat']
    if os.path.getsize(base + '.dict') > 0:
        command += ['-D', base + '.dict']
    with subprocess.Popen(command + [base + '.zst'], stdout=subprocess.PIPE, text=True) as p:
        for line in p.stdout:
            if '"gloss"' in line:
                yield json.loads(line)


LABELS = {'lb', 'lbl', 'label', 'tlb'}
WORDS = {'l': 2, 'm': 2, 'll': 2, 'w': 1, 'taxfmt': 1, 'taxlink': 1, 'vern': 1, 'gloss': 1, 'gl': 1,
         'q': 1, 'qualifier': 1, 'i': 1, 'ngd': 1, 'n-g': 1, 'non-gloss definition': 1, 'sense': 1, 's': 1}


def plain(text):
    """Wikitext as words, as wiktglos: labels and word templates as their words, links as text."""
    def template(m):
        parts = [p.strip() for p in m.group(1).split('|')]
        name, args = parts[0], [p for p in parts[1:] if '=' not in p]
        if name in LABELS:
            return ' ' + ' '.join(a for a in args[1:] if a not in ('_', 'and', 'or', '&')) + ': '
        if name in WORDS and len(args) >= WORDS[name]:
            return ' ' + args[WORDS[name] - 1] + ' '
        return ' '
    for _ in range(3):  # nested templates, inside out
        text = re.sub(r'\{\{([^{}]*)\}\}', template, text)
    text = re.sub(r'\[\[(?:[^\]|]*\|)?([^\]]*)\]\]', r'\1', text)
    text = text.replace("'''", '').replace("''", '')
    return re.sub(r'\s+', ' ', text).strip()


def entries(page):
    """(language, path, node) of each part of speech with senses and glossed tables."""
    def walk(node, code, path):
        for key in ('sections', 'children'):
            for i, child in enumerate(node.get(key, [])):
                p = path + [(key, i)]
                tables = [c for c in child.get('children', []) if c.get('section') == 'Translations']
                if tables and child.get('senses'):
                    yield code, p, child, tables
                yield from walk(child, code, p)
    for language in page['sections']:
        yield from walk(language, language.get('code'), [])


def entry_data(node, tables):
    defs = [(s['id'], plain(s['text'])) for s in node.get('senses', [])]
    for c in node.get('children', []):  # en {{en-verb}} below ===Verb=== of older output
        if c.get('section') == node.get('section') and c.get('senses'):
            defs += [(s['id'], plain(s['text'])) for s in c['senses']]
    glosses = {}
    for t in tables:
        for tr in t.get('translations', []):
            if 'gloss' in tr and 'sense' not in tr:
                glosses.setdefault(tr['gloss'], (tr.get('sense_ids'), tr.get('sense_method'), tr.get('sense_score', 0)))
    return defs, glosses


# Ollama model names and their sentence-transformers (Hugging Face) models
HF_MODELS = {'qwen3-embedding:4b': 'Qwen/Qwen3-Embedding-4B', 'qwen3-embedding:0.6b': 'Qwen/Qwen3-Embedding-0.6B',
             'qwen3-embedding:8b': 'Qwen/Qwen3-Embedding-8B', 'embeddinggemma': 'google/embeddinggemma-300m',
             'nomic-embed-text': 'nomic-ai/nomic-embed-text-v1.5'}


class Embedder:
    """Normalized embeddings of texts, cached on disk; by sentence-transformers on the GPU
    (backend st), else by Ollama (backend ollama); auto: st when it loads, else Ollama."""

    def __init__(self, model, backend='auto', url='http://localhost:11434'):
        self.model, self.url, self.st = model, url, None
        if backend in ('auto', 'st'):
            try:
                from sentence_transformers import SentenceTransformer
                import torch
                name = HF_MODELS.get(model, model)
                self.st = SentenceTransformer(name, device='cuda' if torch.cuda.is_available() else 'cpu',
                                              trust_remote_code=True, model_kwargs={'torch_dtype': torch.float16})
                print(f'sentence-transformers: {name}', file=sys.stderr)
            except Exception as e:  # no package, no model: Ollama
                if backend == 'st':
                    raise
                print(f'sentence-transformers not used ({type(e).__name__}: {str(e)[:120]}), Ollama: {model}',
                      file=sys.stderr)
        self.cache_path = 'glossembed-' + re.sub(r'[^\w.-]', '_', model) + '.npz'
        self.cache = {}
        if os.path.exists(self.cache_path):
            data = np.load(self.cache_path)
            self.cache = dict(zip(data['keys'], data['vectors']))

    @staticmethod
    def key(text):
        return hashlib.sha1(text.encode()).hexdigest()[:16]

    def _compute(self, texts):
        if self.st is not None:
            return self.st.encode(texts, batch_size=128, normalize_embeddings=True, convert_to_numpy=True).astype(np.float32)
        body = json.dumps({'model': self.model, 'input': texts, 'truncate': True}).encode()
        request = urllib.request.Request(self.url + '/api/embed', body, {'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=600) as r:
            vectors = np.array(json.load(r)['embeddings'], dtype=np.float32)
        return vectors / (np.linalg.norm(vectors, axis=1, keepdims=True) + 1e-9)

    def embed(self, texts, batch=1024):
        """All texts at once: the missing ones by length (batches of similar texts), cached"""
        missing = sorted(dict.fromkeys(t for t in texts if self.key(t) not in self.cache), key=len)
        for i in range(0, len(missing), batch):
            part = missing[i:i + batch]
            for t, v in zip(part, self._compute(part)):
                self.cache[self.key(t)] = v
            print(f'\r  embedded {min(i + batch, len(missing))}/{len(missing)}', end='', file=sys.stderr, flush=True)
        if missing:
            print(file=sys.stderr)
            self.save()
        return np.array([self.cache[self.key(t)] for t in texts])

    def save(self):
        if self.cache:
            np.savez(self.cache_path, keys=np.array(list(self.cache)), vectors=np.array(list(self.cache.values())))


def texts(embedder, title, defs, glosses):
    """The texts of an entry to embed: its definitions, its glosses (qwen: as queries)"""
    query = QUERY if 'qwen' in embedder.model.lower() else ''
    return [f'{title}: {text}' for _, text in defs], [f'{query}{title}: {plain(text)}' for text in glosses]


def predict_all(embedder, cases):
    """cases: (title, defs, glosses); for each case and gloss (best index, score, margin over the
    second); the texts of all cases embedded together"""
    parts = [texts(embedder, title, defs, glosses) for title, defs, glosses in cases]
    flat = [t for d, g in parts for t in d + g]
    vectors = embedder.embed(flat)
    results, at = [], 0
    for d, g in parts:
        dv, gv = vectors[at:at + len(d)], vectors[at + len(d):at + len(d) + len(g)]
        at += len(d) + len(g)
        sims = gv @ dv.T
        row_results = []
        for row in sims:
            order = np.argsort(-row)
            best = row[order[0]]
            second = row[order[1]] if len(order) > 1 else -1.0
            row_results.append((int(order[0]), float(best), float(best - second)))
        results.append(row_results)
    return results


def related(a, b):
    """The same sense, or one is a subsense of the other (1 and 1.2)."""
    return a == b or a.startswith(b + '.') or b.startswith(a + '.')


def run_eval(args, embedder):
    random.seed(1)
    cases = []
    for page in pages(args.input, args.wiki):
        for code, path, node, tables in entries(page):
            defs, glosses = entry_data(node, tables)
            if len(defs) < 2:
                continue
            sure = {g: v[0] for g, v in glosses.items() if v[1] == 'positional' and v[2] >= 0.3 and v[0]}
            if sure:
                cases.append((page['title'], defs, sure))
    random.shuffle(cases)
    cases = cases[:args.entries]
    print(f'{len(cases)} entries with confident links', file=sys.stderr)
    results = []
    predicted = predict_all(embedder, [(title, defs, list(sure)) for title, defs, sure in cases])
    for (title, defs, sure), rows in zip(cases, predicted):
        for gloss, (best, score, margin) in zip(sure, rows):
            results.append((margin, score, any(related(defs[best][0], s) for s in sure[gloss])))
    results.sort(reverse=True)
    n = len(results)
    print(f'{n} glosses; accuracy {sum(r[2] for r in results) / n:.3f}')
    for threshold in (0.0, 0.02, 0.05, 0.08, 0.1, 0.15, 0.2):
        kept = [r for r in results if r[0] >= threshold]
        if kept:
            print(f'  margin >= {threshold:.2f}: {len(kept) / n:.1%} kept, accuracy {sum(r[2] for r in kept) / len(kept):.3f}')


def run_apply(args, embedder):
    cases = []
    for page in pages(args.input, args.wiki):
        for code, path, node, tables in entries(page):
            defs, glosses = entry_data(node, tables)
            open_glosses = [g for g, v in glosses.items() if not v[0]]
            if open_glosses and len(defs) >= 2:
                cases.append((page['title'], code, '/'.join(f'{k}:{i}' for k, i in path), defs, open_glosses))
    print(f'{len(cases)} entries, {sum(len(c[4]) for c in cases)} glosses without a link', file=sys.stderr)
    predicted = predict_all(embedder, [(title, defs, glosses) for title, _, _, defs, glosses in cases])
    with open(args.output, 'w', encoding='utf-8') as out:
        out.write('wiki\ttitle\tlanguage\tpath\tgloss\tsense_id\tscore\tmargin\n')
        for (title, code, where, defs, glosses), rows in zip(cases, predicted):
            for gloss, (best, score, margin) in zip(glosses, rows):
                out.write(f"{args.wiki}\t{title}\t{code}\t{where}\t{gloss}\t{defs[best][0]}\t{score:.3f}\t{margin:.3f}\n")
    print(f'written to {args.output}', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--eval', action='store_true')
    mode.add_argument('--apply', action='store_true')
    parser.add_argument('-i', '--input', default='jsonl_senses')
    parser.add_argument('-o', '--output')
    parser.add_argument('--wiki', default='en')
    parser.add_argument('--model', default='qwen3-embedding:4b')
    parser.add_argument('--entries', type=int, default=3000)
    parser.add_argument('--backend', choices=('auto', 'st', 'ollama'), default='auto')
    args = parser.parse_args()
    args.output = args.output or f'glossembed-{args.wiki}.tsv'
    embedder = Embedder(args.model, args.backend)
    (run_eval if args.eval else run_apply)(args, embedder)


if __name__ == '__main__':
    main()
