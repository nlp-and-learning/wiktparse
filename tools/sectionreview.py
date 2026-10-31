#!/usr/bin/env python3
"""sectionreview: the headings of sections a wiki writes whose meaning is not sure (not in
res/wiktionary_section_names.tsv), reviewed once so as not to be looked into again: SQLite table
heading_review (wiki, heading, occurrences in samples1000, example, status, guess, note; status
uncertain: its meaning not sure; mapped: looked up, then mapped in wiktionary_section_names.tsv;
not_section: no name of a section, a number, a page name).

sectionreview.py [-d section_names_review.sqlite] --export res/wiktionary_section_review.tsv
sectionreview.py [-d …] --import res/wiktionary_section_review.tsv
sectionreview.py [-d …] [-i jsonl_uniform] --lookup [wiki...]
    the heading as a word of the language of its wiki in the dictionaries: en.wiktionary (of the
    most languages) and the wiki itself, its parts of speech and senses (the guesses to check);
    pages by the index of the blocks, <dir>/<wiki>.jsonl.idx
"""
import argparse
import json
import os
import re
import sqlite3
import sys

import zstandard

COLUMNS = ['wiki', 'heading', 'occurrences', 'example', 'status', 'guess', 'note']


def open_db(path):
    db = sqlite3.connect(path)
    db.execute('CREATE TABLE IF NOT EXISTS heading_review(wiki TEXT, heading TEXT, occurrences INTEGER, '
               'example TEXT, status TEXT, guess TEXT, note TEXT, PRIMARY KEY(wiki, heading))')
    return db


def export(db, path):
    rows = db.execute('SELECT ' + ', '.join(COLUMNS) + ' FROM heading_review ORDER BY wiki, occurrences DESC').fetchall()
    with open(path, 'w', encoding='utf-8') as f:
        f.write('# Headings of sections whose meaning was not sure, reviewed (tools/sectionreview.py): status\n'
                '# uncertain (not mapped in wiktionary_section_names.tsv), mapped (looked up, then mapped there),\n'
                '# not_section (a number, a page name, a language).\n')
        f.write('\t'.join(COLUMNS) + '\n')
        for row in rows:
            f.write('\t'.join(re.sub(r'[\t\n]', ' ', str(v if v is not None else '')) for v in row) + '\n')
    print(f'{len(rows)} headings -> {path}')


def import_tsv(db, path):
    n = 0
    with open(path, encoding='utf-8') as f:
        for line in f:
            if line.startswith('#') or line.startswith('wiki\t'):
                continue
            row = line.rstrip('\n').split('\t')
            if len(row) == len(COLUMNS):
                db.execute('INSERT OR REPLACE INTO heading_review VALUES(?,?,?,?,?,?,?)', row)
                n += 1
    db.commit()
    print(f'{n} headings <- {path}')


class Pages:
    """the pages of <dir>/<wiki>.jsonl.zst by title, through its index of blocks"""

    def __init__(self, directory, wiki):
        base = os.path.join(directory, wiki + '.jsonl')
        self.base, self.blocks, self.titles = base, [], {}
        if not os.path.exists(base + '.idx'):
            return
        block = None
        with open(base + '.idx', encoding='utf-8') as f:
            for line in f:
                line = line.rstrip('\n')
                if line[:1].isdigit():
                    block = int(line.split(':', 1)[0])
                    self.blocks.append(block)
                elif line.startswith(':'):
                    start, title = line[1:].split(':', 1)
                    self.titles.setdefault(title, (block, int(start)))
        self.ends = dict(zip(self.blocks, self.blocks[1:] + [os.path.getsize(base + '.zst')]))
        data = open(base + '.dict', 'rb').read() if os.path.exists(base + '.dict') else b''
        self.decompressor = zstandard.ZstdDecompressor(dict_data=zstandard.ZstdCompressionDict(data) if data else None)

    def get(self, title):
        if title not in self.titles:
            return None
        block, start = self.titles[title]
        with open(self.base + '.zst', 'rb') as f:
            f.seek(block)
            data = self.decompressor.decompressobj().decompress(f.read(self.ends[block] - block))
        end = data.find(b'\n', start)
        return json.loads(data[start:end if end >= 0 else len(data)].decode('utf-8'))


def word_of(heading):
    """the word of a heading: of a template its last parameter of letters ({{S|buḍ}}: buḍ), of a
    link its text, without brackets and colons"""
    h = heading.strip()
    m = re.fullmatch(r'\{\{(.*)\}\}', h)
    if m:
        parts = [p for p in m.group(1).split('|') if p and '=' not in p]
        h = parts[1] if len(parts) > 1 and len(parts[1]) > 2 else parts[0]
    h = re.sub(r'\[\[(?:[^\]|]*\|)?([^\]]*)\]\]', r'\1', h)
    h = re.sub(r'<[^>]+>', '', h)
    return h.strip(' :')


def senses_of(page, code):
    """the parts of speech and senses of the language of a code on a page"""
    out = []
    for language in (page or {}).get('languages', []):
        if language.get('code') != code:
            continue
        for e in language.get('entries', []):
            senses = '; '.join(re.sub(r'\s+', ' ', s.get('text', ''))[:80] for s in e.get('senses', [])[:3])
            out.append(f"{e.get('pos')}: {senses}")
    return out


def lookup(db, directory, wikis):
    en = Pages(directory, 'en')
    own = {}
    rows = db.execute('SELECT wiki, heading, guess FROM heading_review WHERE status = "uncertain" ORDER BY wiki').fetchall()
    for wiki, heading, guess in rows:
        if wikis and wiki not in wikis:
            continue
        word = word_of(heading)
        code = wiki.replace('_', '-')
        if wiki not in own:
            own[wiki] = Pages(directory, wiki)
        found = []
        for source, pages in (('en', en), (wiki, own[wiki])):
            for title in dict.fromkeys([word, word.lower(), word[:1].upper() + word[1:]]):
                if senses := senses_of(pages.get(title), code):
                    found.append(f'{source}:{title}: ' + ' | '.join(senses))
                    break
        print(f'{wiki}\t{heading}\t(guess: {guess})')
        for line in found or ['  —']:
            print('    ' + line)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-d', default='section_names_review.sqlite')
    parser.add_argument('-i', default='jsonl_uniform', help='the output of wikt2jsonl --uniform --dump')
    parser.add_argument('--export', metavar='TSV')
    parser.add_argument('--import', dest='import_tsv', metavar='TSV')
    parser.add_argument('--lookup', action='store_true')
    parser.add_argument('wiki', nargs='*')
    args = parser.parse_args()
    db = open_db(args.d)
    if args.import_tsv:
        import_tsv(db, args.import_tsv)
    if args.export:
        export(db, args.export)
    if args.lookup:
        lookup(db, args.i, args.wiki)
    if not (args.import_tsv or args.export or args.lookup):
        parser.print_help(sys.stderr)


if __name__ == '__main__':
    main()
