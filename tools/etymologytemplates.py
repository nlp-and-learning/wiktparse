#!/usr/bin/env python3
"""etymologytemplates: the templates of the etymologies of a wiki not read yet (etymology_unparsed of
wikt2jsonl --uniform), most first, with examples of their parameters, and the words between them;
to write the etymology configuration of the wiki (res/wiktionary/<wiki>.json, doc/uniform-tree.md,
Etymology chain).

etymologytemplates.py [-i uniform1000] [-n 15] [--examples 3] [--all] wiki...
--all: of all etymologies, not only of those not read.
"""
import argparse
import collections
import json
import os
import re

TEMPLATE = re.compile(r'\{\{([^{}]*)\}\}')
WORD = re.compile(r'[^\W\d_]+\.?|[<>+]')
CONFIG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'res', 'wiktionary')


def configured(wiki):
    """the template names of the wiki's etymology configuration (with its profile)"""
    names = set()
    try:
        with open(os.path.join(CONFIG_DIR, wiki + '.json'), encoding='utf-8') as f:
            etymology = json.load(f).get('etymology', {})
    except (OSError, ValueError):
        return names
    with open(os.path.join(CONFIG_DIR, 'etymology_profiles.json'), encoding='utf-8') as f:
        profile = json.load(f)['profiles'].get(etymology.get('profile'), {})
    for spec in (profile, etymology):
        names |= set(spec.get('templates', {})) | set(spec.get('marker_templates', {}))
        names |= set(spec.get('chain_template', {}).get('names', []))
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-i', default='uniform1000', help='output of wikt2jsonl --uniform')
    parser.add_argument('-n', type=int, default=15, help='templates and words shown')
    parser.add_argument('--examples', type=int, default=3)
    parser.add_argument('--all', action='store_true', help='all etymologies, not only those not read')
    parser.add_argument('wiki', nargs='+')
    args = parser.parse_args()
    for wiki in args.wiki:
        path = os.path.join(args.i, wiki + '.jsonl')
        if not os.path.exists(path):
            print(f'{wiki}: no {path}')
            continue
        known = configured(wiki)
        pages = collections.Counter()   # template -> etymologies
        examples = collections.defaultdict(list)
        words = collections.Counter()
        total = chosen = 0
        with open(path, encoding='utf-8') as f:
            for line in f:
                for language in json.loads(line)['languages']:
                    for x in [language] + language.get('entries', []):
                        text = x.get('etymology', '')
                        if not text.strip():
                            continue
                        total += 1
                        if not args.all and not x.get('etymology_unparsed'):
                            continue
                        chosen += 1
                        seen = set()
                        for m in TEMPLATE.finditer(text):
                            name = m.group(1).split('|')[0].strip()
                            if name not in seen:
                                seen.add(name)
                                pages[name] += 1
                                if len(examples[name]) < args.examples:
                                    examples[name].append(m.group(0)[:100])
                        rest = TEMPLATE.sub(' ', re.sub(r'<ref[^>]*>.*?</ref>|<[^>]+>', ' ', text, flags=re.S))
                        rest = re.sub(r'\[\[[^\]]*\]\]', ' ', rest)
                        words.update(w.lower() for w in WORD.findall(rest))
        print(f'== {wiki}: {chosen} of {total} etymologies' + ('' if args.all else ' not read'))
        for name, n in pages.most_common(args.n):
            mark = ' (configured)' if name in known else ''
            print(f'  {n:6}  {name}{mark}')
            for e in examples[name]:
                print(f'          {e}')
        print('  words: ' + ', '.join(f'{w} {n}' for w, n in words.most_common(args.n)))


if __name__ == '__main__':
    main()
