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

unknownheadings.py [-d .] [-o unknown_headings.tsv] [--min-pages 4] [wiki...]
Output: pages, wiki, levels (2,3), kind (language or section, by the level of the language headings of the wiki), heading.
"""
import argparse
import collections
import glob
import json
import os
import re
import sqlite3
import unicodedata

TEMPLATE = re.compile(r'^\{\{([^{}]*)\}\}')
NUMBER = re.compile(r'^(.*[^\d\s.])\s*(?:Nr\.|№)?\s*\d+$')
LINK = re.compile(r'\[\[(?:[^\]|]*\|)?([^\]|]*)\]\]')
MARKUP = re.compile(r"<[^>]*>|''+")
# Wiktionaries whose Wikidata label language differs (as wikt2jsonl)
LABEL_LANGUAGE = {'no': 'nb', 'simple': 'en', 'zh-min-nan': 'nan', 'roa-rup': 'rup', 'bh': 'bho'}


def merged(base, patch):
    """patch over base, objects merged (as wikt2jsonl's mergeInto)"""
    result = dict(base)
    for key, value in patch.items():
        result[key] = merged(result[key], value) if isinstance(value, dict) and isinstance(result.get(key), dict) else value
    return result


def language_levels(config_dir):
    """wiki -> the heading levels of its language headings (by its profile, its configuration and
    the other forms of language.also; none for markers on lines of their own, profile E)"""
    profiles = json.load(open(os.path.join(config_dir, 'profiles.json'), encoding='utf-8'))['profiles']

    def profile(name):
        p = profiles.get(name, {})
        return merged(profile(p['base']), p) if 'base' in p else p

    levels = {}
    with open(os.path.join(config_dir, 'wikis.tsv'), encoding='utf-8') as f:
        for line in f:
            fields = line.rstrip('\n').split('\t')
            if len(fields) < 2 or fields[0] == 'wiki':
                continue
            config = profile(fields[1])
            if len(fields) > 3 and fields[3]:
                try:
                    with open(os.path.join(config_dir, fields[3]), encoding='utf-8') as c:
                        config = merged(config, json.load(c))
                except (OSError, ValueError):
                    pass
            language = config.get('language', {})
            found = set()
            for form in [language] + [merged(language, a) for a in language.get('also', [])]:
                if str(form.get('form', '')).startswith('heading'):
                    found.add(int(form.get('heading_level', 2)))
            levels[fields[0]] = found
    return levels


def plain(text):
    """without combining characters (vowel marks, accents of a decomposed text)"""
    return ''.join(c for c in unicodedata.normalize('NFD', text) if not unicodedata.combining(c))


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
        # as the parser: of a heading with a template not at its start, the text before it, as
        # written (te "బ్రౌను నిఘంటువు నుండి<ref name=brown>{{…}}</ref>")
        raw = heading.strip().strip('=').strip()
        if (at := raw.find('{{')) > 0:
            found += [raw[:at].strip(), MARKUP.sub('', raw[:at]).strip()]
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
    # (headings of 1 to 3 pages are mostly words written as headings, not sections: 91% of the rows,
    # 14% of the pages)
    parser.add_argument('--min-pages', type=int, default=4, help='headings of fewer pages left out (1: all)')
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
    # whole words of the names of a language in a language, of one language only (as the parser:
    # gor "Hulontalo" of "Bahasa Hulontalo", pl "polski" of "język polski"; 4 letters at least)
    parts = collections.defaultdict(dict)  # in_code -> word -> code, None when of several languages
    for code, in_code, name in langs.execute('SELECT code, in_code, name FROM language_name'):
        names[in_code].add(name.lower())
        words = re.split(r'[ -]', name.lower())
        for word in words if len(words) > 1 else []:
            if len(word) >= 4:
                seen = parts[in_code].get(word, code)
                parts[in_code][word] = code if seen == code else None
    # English names of languages also the English aliases (gor: Wikidata alias "Hulontalo")
    english |= names['en']
    for lang, name in sections.execute('SELECT lang, name FROM section_name'):
        names[lang].add(name.lower())
    # Language templates of a wiki's configuration (lt {{ltv}}: "template_regex": "^([a-z]{2,3})v$")
    config_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'res', 'wiktionary')
    # and its language aliases (mg "roa-nor": "nrf")
    # and its lookup pattern of names (ar "ال{name}": إنجليزية as الإنجليزية)
    # and words left out of names (ar "لغة": language)
    template_regex, aliases, lookup, strip = {}, collections.defaultdict(set), {}, collections.defaultdict(list)
    for path in glob.glob(os.path.join(config_dir, '*.json')):
        try:
            with open(path, encoding='utf-8') as f:
                language = json.load(f).get('language', {})
        except (OSError, ValueError, AttributeError):
            continue
        wiki = os.path.basename(path)[:-5]
        if regex := language.get('template_regex'):
            template_regex[wiki] = re.compile(regex)
        aliases[wiki] = {a.lower() for a in language.get('aliases', {})}
        if pattern := language.get('lookup'):
            lookup[wiki] = pattern
        strip[wiki] = [w.lower() for w in language.get('strip', [])]
    language_level = language_levels(config_dir)
    rows = []
    for path in sorted(glob.glob(os.path.join(args.d, 'headers', '*.tsv'))):
        wiki = os.path.basename(path)[:-4]
        if args.wiki and wiki not in args.wiki:
            continue
        lang = wiki.replace('_', '-')
        lang = LABEL_LANGUAGE.get(lang, lang)  # no.wikt is in Bokmål: Wikidata labels of nb
        known = names[wiki] | names[lang] | english | codes | aliases[wiki]
        pages, levels = collections.Counter(), collections.defaultdict(collections.Counter)  # heading -> level -> pages
        with open(path, encoding='utf-8') as f:
            for line in f:
                p = line.rstrip('\n').split('\t')
                if len(p) < 4 or p[0] != '(main)' or not p[1].isdigit():
                    continue
                heading = p[3].strip().strip('=').strip()
                if '(' in heading and ('{{' in heading or '[[' in heading):
                    continue
                k = keys(heading)
                # cleaned as the parser: without vowel marks (ar "عَرَبِيَّة"), without a word as "language"
                k += [x for x in (plain(y) for y in k) if x not in k]
                k += [' '.join(y.replace(w, ' ').split()) for y in list(k) for w in strip[wiki] if w in y]
                if wiki in lookup:
                    k += [lookup[wiki].replace('{name}', x) for x in k]
                if not k or any(x in known for x in k):
                    continue
                if any(parts[lang].get(x) for x in k if len(x) >= 4):
                    continue
                if (r := template_regex.get(wiki)) and any(
                        (m := r.match(x)) and m.group(1).lower() in codes for x in k):
                    continue
                pages[heading] += int(p[2])
                levels[heading][int(p[1])] += int(p[2])
        # kind: a language when most of its pages have it at the level of the wiki's language
        # headings, else a section (a guess: some wikis have sections at that level too, te, ar)
        lang_levels = language_level.get(wiki, set())
        rows += [(n, wiki, ','.join(map(str, sorted(levels[h]))),
                  'language' if 2 * sum(c for lv, c in levels[h].items() if lv in lang_levels) > n else 'section', h)
                 for h, n in pages.items() if n >= args.min_pages]
    rows.sort(key=lambda r: -r[0])
    with open(args.o, 'w', encoding='utf-8') as out:
        out.write('pages\twiki\tlevels\tkind\theading\n')
        for r in rows:
            out.write('\t'.join(map(str, r)) + '\n')
    print(f'{len(rows)} unknown headings ({sum(r[0] for r in rows)} pages) -> {args.o}')


if __name__ == '__main__':
    main()
