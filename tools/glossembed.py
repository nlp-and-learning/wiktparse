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

Embeddings come from Ollama (/api/embed), cached in glossembed-<model>.npz.
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


class Embedder:
    def __init__(self, model, url='http://localhost:11434'):
        self.model, self.url = model, url
        self.cache_path = 'glossembed-' + re.sub(r'[^\w.-]', '_', model) + '.npz'
        self.cache = {}
        if os.path.exists(self.cache_path):
            data = np.load(self.cache_path)
            self.cache = dict(zip(data['keys'], data['vectors']))

    @staticmethod
    def key(text):
        return hashlib.sha1(text.encode()).hexdigest()[:16]

    def embed(self, texts, batch=128):
        missing = list(dict.fromkeys(t for t in texts if self.key(t) not in self.cache))
        for i in range(0, len(missing), batch):
            part = missing[i:i + batch]
            body = json.dumps({'model': self.model, 'input': part, 'truncate': True}).encode()
            request = urllib.request.Request(self.url + '/api/embed', body, {'Content-Type': 'application/json'})
            with urllib.request.urlopen(request, timeout=600) as r:
                vectors = np.array(json.load(r)['embeddings'], dtype=np.float32)
            vectors /= np.linalg.norm(vectors, axis=1, keepdims=True) + 1e-9
            for t, v in zip(part, vectors):
                self.cache[self.key(t)] = v
            print(f'\r  embedded {min(i + batch, len(missing))}/{len(missing)}', end='', file=sys.stderr, flush=True)
        if missing:
            print(file=sys.stderr)
        return np.array([self.cache[self.key(t)] for t in texts])

    def save(self):
        if self.cache:
            np.savez(self.cache_path, keys=np.array(list(self.cache)), vectors=np.array(list(self.cache.values())))


def predict(embedder, title, defs, glosses):
    """For each gloss: (best index, score, margin over the second)."""
    query = QUERY if 'qwen' in embedder.model else ''
    d = embedder.embed([f'{title}: {text}' for _, text in defs])
    g = embedder.embed([f'{query}{title}: {plain(text)}' for text in glosses])
    sims = g @ d.T
    result = []
    for row in sims:
        order = np.argsort(-row)
        best = row[order[0]]
        second = row[order[1]] if len(order) > 1 else -1.0
        result.append((int(order[0]), float(best), float(best - second)))
    return result


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
    for title, defs, sure in cases:
        glosses = list(sure)
        for gloss, (best, score, margin) in zip(glosses, predict(embedder, title, defs, glosses)):
            results.append((margin, score, any(related(defs[best][0], s) for s in sure[gloss])))
    embedder.save()
    results.sort(reverse=True)
    n = len(results)
    print(f'{n} glosses; accuracy {sum(r[2] for r in results) / n:.3f}')
    for threshold in (0.0, 0.02, 0.05, 0.08, 0.1, 0.15, 0.2):
        kept = [r for r in results if r[0] >= threshold]
        if kept:
            print(f'  margin >= {threshold:.2f}: {len(kept) / n:.1%} kept, accuracy {sum(r[2] for r in kept) / len(kept):.3f}')


def run_apply(args, embedder):
    out = open(args.output, 'w', encoding='utf-8')
    out.write('wiki\ttitle\tlanguage\tpath\tgloss\tsense_id\tscore\tmargin\n')
    count = 0
    for page in pages(args.input, args.wiki):
        for code, path, node, tables in entries(page):
            defs, glosses = entry_data(node, tables)
            open_glosses = [g for g, v in glosses.items() if not v[0]]
            if not open_glosses or len(defs) < 2:
                continue
            for gloss, (best, score, margin) in zip(open_glosses, predict(embedder, page['title'], defs, open_glosses)):
                where = '/'.join(f'{k}:{i}' for k, i in path)
                out.write(f"{args.wiki}\t{page['title']}\t{code}\t{where}\t{gloss}\t{defs[best][0]}\t{score:.3f}\t{margin:.3f}\n")
                count += 1
        if count and count % 5000 < 3:
            embedder.save()
    embedder.save()
    print(f'{count} glosses written to {args.output}', file=sys.stderr)


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
    args = parser.parse_args()
    args.output = args.output or f'glossembed-{args.wiki}.tsv'
    embedder = Embedder(args.model)
    (run_eval if args.eval else run_apply)(args, embedder)


if __name__ == '__main__':
    main()
