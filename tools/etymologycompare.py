#!/usr/bin/env python3
"""etymologycompare: the etymology chains of one entry in two wikis (wikt2jsonl --uniform output),
an entry by its title and language: pl.wikt woda and the Polish section of en.wikt woda. The pages
of en.wikt of the titles of the samples of the other wikis: wiktsamples --same-titles samples100
-o same100 en, then wikt2jsonl --uniform -i same100 -o uniform_same100 en.

The steps of a chain: its items of from, borrowed, calque, compound, root of no other item, a
compound as its parts (ro "from it abbeverare | from it -aggio" and en "compound it abbeverare +
-aggio" have the same steps); languages of a variety as their language (fa-cls fa, la-lat la), forms
without accents and *. Each entry of a chain in both wikis is one of:
  same        the first steps are the same
  consistent  a step in both, or a form of a step in both by other codes (ro el μοναχός, en.wikt grc
              μοναχός): one chain starts earlier or later
  continues   no step in both, but the languages of one chain are ancestors of those of the other
              (wiktlangs.sqlite: ancestors, the proto-language of a family): one goes on where the
              other stops (en.wikt pl woda from zlw-opl woda, "further at Old Polish"; pl.wikt from
              sla-pro *voda)
  apart       no step in both, a language of a step in both (other forms of it)
  conflict    no language of a step in both: a parser error or different etymologies
  one-sided   steps in one chain only (cognates only in the other)
Examples of the conflicts and of the apart entries follow.

etymologycompare.py [-i uniform100] [--en uniform_same100/en.jsonl] [--db wiktlangs.sqlite] [--examples 3] [wiki...]
"""
import argparse
import collections
import functools
import glob
import json
import os
import sqlite3
import unicodedata

STEPS = {'from', 'borrowed', 'calque', 'compound', 'root'}
KINDS = ('same', 'consistent', 'continues', 'apart', 'conflict', 'one-sided')
LANGUAGES = {}  # code -> (kind, family, parent, ancestors) of wiktlangs.sqlite


def load_languages(path):
    if os.path.exists(path):
        db = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
        for code, kind_, family, parent, ancestors in db.execute(
                'SELECT code, kind, family, parent, ancestors FROM language'):
            LANGUAGES[code] = (kind_, family, parent, ancestors)


@functools.lru_cache(maxsize=None)
def lineage(code):
    """the ancestors of a language: its ancestors, the language of a variety (parent), the
    proto-languages of its families (zlw-opl: zlw-lch-pro, zlw-pro, sla-pro, ine-bsl-pro, ine-pro)"""
    out, todo = set(), [code]
    while todo:
        c = todo.pop()
        kind_, family, parent, ancestors = LANGUAGES.get(c, (None, None, None, None))
        nxt = [a.strip() for a in (ancestors or '').split(',') if a.strip()]
        if parent:
            nxt.append(parent)
        if kind_ == 'family':  # (a family: its proto-language, the family above it)
            nxt.append(c + '-pro')
        elif family:
            nxt.append(family)
        for n in nxt:
            if n not in out and n != code:
                out.add(n)
                todo.append(n)
    return frozenset(n for n in out if LANGUAGES.get(n, ('family',))[0] != 'family')


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
        parts += [''] * (3 - len(parts))
        out.append((parts[0], parts[1], ' '.join(parts[2:]), of))
    return out


def plain(form):
    """a form to compare: lowercase, no accents or vowel marks, no * of reconstructions"""
    form = unicodedata.normalize('NFD', form.lower().replace('*', '').strip())
    return ''.join(c for c in form if not unicodedata.combining(c))


def language(code):
    """a variety as its language (fa-cls fa, la-lat la, ms-cla ms); proto-languages and families as
    they are (sla-pro, zlw-opl)"""
    head = code.split('-')[0]
    return head if len(head) == 2 else code


def steps(chain):
    """the steps of a chain in order: (language, form, the code as written), a compound as its parts"""
    out = []
    for relation, lang, form, of in items(chain):
        if relation in STEPS and not of and form:
            for part in form.split(' + '):
                if (language(lang), plain(part)) not in [s[:2] for s in out]:
                    out.append((language(lang), plain(part), lang))
    return out


def continues(sa, sb):
    """the languages of the steps of one chain all ancestors of those of the other (and not the same)"""
    codes_a, codes_b = {s[2] for s in sa}, {s[2] for s in sb}
    below = set().union(*(lineage(c) for c in codes_a))
    return bool(codes_b) and codes_b <= below


def chains(path):
    """(title, language) -> the chains of its etymologies"""
    out = {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            page = json.loads(line)
            for lang in page['languages']:
                for x in [lang] + lang.get('entries', []):
                    if 'etymology_chain' in x:
                        out.setdefault((page['title'], lang.get('code')), []).append(x['etymology_chain'])
    return out


def kind(a, b):
    """the kind of a pair of chains (KINDS)"""
    sa, sb = steps(a), steps(b)
    if not sa or not sb:
        return 'one-sided'
    if sa[0][:2] == sb[0][:2]:
        return 'same'
    if {s[:2] for s in sa} & {s[:2] for s in sb} or {s[1] for s in sa} & {s[1] for s in sb}:
        return 'consistent'
    if continues(sa, sb) or continues(sb, sa):
        return 'continues'
    if {s[0] for s in sa} & {s[0] for s in sb}:
        return 'apart'
    return 'conflict'


def compare(own, en):
    """the best pair of chains of the entry (of its etymologies): (kind, own chain, en chain)"""
    return min(((kind(a, b), a, b) for a in own for b in en), key=lambda r: KINDS.index(r[0]))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-i', default='uniform100')
    parser.add_argument('--en', default='uniform_same100/en.jsonl')
    parser.add_argument('--db', default='wiktlangs.sqlite', help='the ancestors of languages')
    parser.add_argument('--examples', type=int, default=3)
    parser.add_argument('wiki', nargs='*')
    args = parser.parse_args()
    load_languages(args.db)
    en = chains(args.en)
    total = collections.Counter()

    def counts(c):
        n = sum(c.values())
        return f'{n} entries: ' + ', '.join(f'{k} {c[k]} ({100 * c[k] // n}%)' for k in KINDS if c[k])

    for path in sorted(glob.glob(os.path.join(args.i, '*.jsonl'))):
        wiki = os.path.basename(path)[:-6]
        if wiki == 'en' or (args.wiki and wiki not in args.wiki):
            continue
        pairs = [(key, compare(own, en[key])) for key, own in chains(path).items() if key in en]
        if not pairs:
            continue
        c = collections.Counter(r[0] for _, r in pairs)
        total += c
        print(f'{wiki}: {counts(c)}')
        for want in ('conflict', 'apart'):
            for (title, lang), (k, a, b) in [p for p in pairs if p[1][0] == want][:args.examples]:
                print(f'  {k} {title} ({lang})\n    {wiki}: ' + a.replace('\n', ' | ')[:200] +
                      '\n    en: ' + b.replace('\n', ' | ')[:200])
    if total:
        print(f'all: {counts(total)}')


if __name__ == '__main__':
    main()
