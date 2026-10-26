#!/usr/bin/env python3
"""unknownheadings: the headings of the dumps (wiktheaders: headers/<wiki>.tsv, main namespace)
known neither as a language (wiktlangs.sqlite: names in the wiki's language, English names,
codes) nor as a section (wiktsections.sqlite: names of the wiki, in its language, English
sections), by pages, most first; to name them in res/wiktionary_section_names.tsv (sections) or
in the wiki's configuration (languages).

Headings as the parser keys them: without '=' at the ends, links as their text, of a template its
name (without '-', '=') and its unnamed parameters, a number at the end left out ("詞源2",
"Izruna Nr.2"); headings of a word and a language in parentheses (pl "woda ({{język polski}})")
are skipped.

unknownheadings.py [-d .] [-o unknown_headings.tsv] [--min-pages 1] [wiki...]
Output: pages, wiki, levels (2,3), heading.
"""
import argparse
import collections
import glob
import json
import os
import re
import sqlite3

TEMPLATE = re.compile(r'^\{\{([^{}]*)\}\}')
NUMBER = re.compile(r'^(.*[^\d\s.])\s*(?:Nr\.|№)?\s*\d+$')
LINK = re.compile(r'\[\[(?:[^\]|]*\|)?([^\]|]*)\]\]')
MARKUP = re.compile(r"<[^>]*>|''+")
# Wiktionaries whose Wikidata label language differs (as wikt2jsonl)
LABEL_LANGUAGE = {'no': 'nb', 'simple': 'en', 'zh-min-nan': 'nan', 'roa-rup': 'rup', 'bh': 'bho'}


def keys(heading):
    h = LINK.sub(r'\1', heading.strip().strip('=').strip())
    h = MARKUP.sub('', h).strip()
    found = []
    m = TEMPLATE.match(h)
    if m:
        parts = [p.strip() for p in m.group(1).split('|')]
        found += [parts[0].strip('-='), parts[0]] + [p for p in parts[1:] if '=' not in p]
    else:
        found.append(h)
    result = []
    for k in found:
        if k:
            result.append(k.lower())
            if n := NUMBER.match(k):
                result.append(n.group(1).lower())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-d', default='.', help='directory of headers/, wiktlangs.sqlite, wiktsections.sqlite')
    parser.add_argument('-o', default='unknown_headings.tsv')
    parser.add_argument('--min-pages', type=int, default=1)
    parser.add_argument('wiki', nargs='*')
    args = parser.parse_args()
    langs = sqlite3.connect(os.path.join(args.d, 'wiktlangs.sqlite'))
    sections = sqlite3.connect(os.path.join(args.d, 'wiktsections.sqlite'))
    codes = {r[0].lower() for r in langs.execute('SELECT code FROM language')}
    # and other codes of them (mg {{=zho=}}: ISO 639-3 zho -> zh), as the parser maps them
    codes |= {r[0].lower() for r in langs.execute('SELECT code FROM code_map')}
    english = {r[0].lower() for r in langs.execute('SELECT name FROM language') if r[0]}
    english |= {r[0].lower() for r in sections.execute('SELECT section FROM section')}
    names = collections.defaultdict(set)  # language or wiki -> lowercase names
    for in_code, name in langs.execute('SELECT in_code, name FROM language_name'):
        names[in_code].add(name.lower())
    for lang, name in sections.execute('SELECT lang, name FROM section_name'):
        names[lang].add(name.lower())
    # Language templates of a wiki's configuration (lt {{ltv}}: "template_regex": "^([a-z]{2,3})v$")
    config_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'res', 'wiktionary')
    template_regex = {}
    for path in glob.glob(os.path.join(config_dir, '*.json')):
        try:
            with open(path, encoding='utf-8') as f:
                regex = json.load(f).get('language', {}).get('template_regex')
        except (OSError, ValueError, AttributeError):
            continue
        if regex:
            template_regex[os.path.basename(path)[:-5]] = re.compile(regex)
    rows = []
    for path in sorted(glob.glob(os.path.join(args.d, 'headers', '*.tsv'))):
        wiki = os.path.basename(path)[:-4]
        if args.wiki and wiki not in args.wiki:
            continue
        lang = wiki.replace('_', '-')
        lang = LABEL_LANGUAGE.get(lang, lang)  # no.wikt is in Bokmål: Wikidata labels of nb
        known = names[wiki] | names[lang] | english | codes
        pages, levels = collections.Counter(), collections.defaultdict(set)
        with open(path, encoding='utf-8') as f:
            for line in f:
                p = line.rstrip('\n').split('\t')
                if len(p) < 4 or p[0] != '(main)' or not p[1].isdigit():
                    continue
                heading = p[3].strip().strip('=').strip()
                if '(' in heading and ('{{' in heading or '[[' in heading):
                    continue
                k = keys(heading)
                if not k or any(x in known for x in k):
                    continue
                if (r := template_regex.get(wiki)) and any(
                        (m := r.match(x)) and m.group(1).lower() in codes for x in k):
                    continue
                pages[heading] += int(p[2])
                levels[heading].add(int(p[1]))
        rows += [(n, wiki, ','.join(map(str, sorted(levels[h]))), h) for h, n in pages.items() if n >= args.min_pages]
    rows.sort(key=lambda r: -r[0])
    with open(args.o, 'w', encoding='utf-8') as out:
        out.write('pages\twiki\tlevels\theading\n')
        for r in rows:
            out.write('\t'.join(map(str, r)) + '\n')
    print(f'{len(rows)} unknown headings ({sum(r[0] for r in rows)} pages) -> {args.o}')


if __name__ == '__main__':
    main()
