#!/usr/bin/env python3
"""etymologycompare: the etymology chains of one entry in two wikis (wikt2jsonl --uniform output),
an entry by its title and language: pl.wikt woda and the Polish section of en.wikt woda. The pages
of en.wikt of the titles of the samples of the other wikis: wiktsamples --same-titles samples100
-o same100 en, then wikt2jsonl --uniform -i same100 -o uniform_same100 en.

For each wiki: the entries with a chain in both; of them those whose first step (the first item of
from, borrowed, calque) is of the same language, and the share of the items (language, form) of
the wiki found in en.wikt; examples of the entries that differ.

etymologycompare.py [-i uniform100] [--en uniform_same100/en.jsonl] [--examples 3] [wiki...]
"""
import argparse
import glob
import json
import os
import unicodedata

CHAIN = {'from', 'borrowed', 'calque'}


def items(chain):
    """the items of a chain: (relation, language, form, of an item)"""
    out = []
    for line in chain.split('\n'):
        parts = line.split(' ')[1:]  # (without "n.")
        if parts and parts[-1] == '?':
            parts = parts[:-1]
        of = parts[-1] if parts and parts[-1].startswith('[') else None
        if of:
            parts = parts[:-1]
        relation, lang, form = (parts + ['', '', ''])[:3] if len(parts) < 3 else (parts[0], parts[1], ' '.join(parts[2:]))
        out.append((relation, lang, form, of))
    return out


def plain(form):
    """a form to compare: lowercase, no marks of vowels or accents, no * of reconstructions"""
    form = unicodedata.normalize('NFD', form.lower().replace('*', '').strip())
    return ''.join(c for c in form if not unicodedata.combining(c))


def chains(path):
    """(title, language) -> the chains of its etymologies"""
    out = {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            page = json.loads(line)
            for language in page['languages']:
                for x in [language] + language.get('entries', []):
                    if 'etymology_chain' in x:
                        out.setdefault((page['title'], language.get('code')), []).append(x['etymology_chain'])
    return out


def first_step(chain):
    for relation, lang, form, of in items(chain):
        if relation in CHAIN and not of:
            return lang
    return None


def compare(own, en):
    """the best pair of chains of the entry: (the same first step, the share of the items found)"""
    best = None
    for a in own:
        steps = {(lang, plain(form)) for relation, lang, form, of in items(a) if form}
        for b in en:
            found = {(lang, plain(form)) for relation, lang, form, of in items(b) if form}
            share = len(steps & found) / len(steps) if steps else 0
            same = first_step(a) is not None and first_step(a) == first_step(b)
            if best is None or (same, share) > best[:2]:
                best = (same, share, a, b)
    return best


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-i', default='uniform100')
    parser.add_argument('--en', default='uniform_same100/en.jsonl')
    parser.add_argument('--examples', type=int, default=3)
    parser.add_argument('wiki', nargs='*')
    args = parser.parse_args()
    en = chains(args.en)
    total = [0, 0, 0.0]
    for path in sorted(glob.glob(os.path.join(args.i, '*.jsonl'))):
        wiki = os.path.basename(path)[:-6]
        if wiki == 'en' or (args.wiki and wiki not in args.wiki):
            continue
        pairs = [(key, compare(own, en[key])) for key, own in chains(path).items() if key in en]
        if not pairs:
            continue
        same = sum(1 for _, r in pairs if r[0])
        share = sum(r[1] for _, r in pairs) / len(pairs)
        total[0] += len(pairs)
        total[1] += same
        total[2] += share * len(pairs)
        print(f'{wiki}: {len(pairs)} entries, the same first step {same} ({100 * same // len(pairs)}%), '
              f'items found in en {100 * share:.0f}%')
        for (title, lang), (_, s, a, b) in [p for p in pairs if not p[1][0]][:args.examples]:
            print(f'  {title} ({lang}), {100 * s:.0f}%\n    {wiki}: ' + a.replace('\n', ' | ')[:200] +
                  '\n    en: ' + b.replace('\n', ' | ')[:200])
    if total[0]:
        print(f'all: {total[0]} entries, the same first step {total[1]} ({100 * total[1] // total[0]}%), '
              f'items found in en {100 * total[2] / total[0]:.0f}%')


if __name__ == '__main__':
    main()
