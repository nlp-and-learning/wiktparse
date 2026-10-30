#!/usr/bin/env python3
"""etymologywords: the words of description of the etymologies of each wiki (wikt2jsonl --uniform
output), not the words they come from: the text of an etymology without its templates, links,
italics, quotations and notes ("From", "cognate with", "von", "vgl.", "od"); phrases of one to three
words, how many etymologies have them, an example. Into SQLite (etymology_words.sqlite, table
etymology_word), with the relation of a phrase known to res/wiktionary/etymology_words.tsv or to
the configuration of the wiki, to give the others a relation by hand and take them into the TSV
(--export).

etymologywords.py [-i uniform10] [-o etymology_words.sqlite] [--min 2] [--show 20] [wiki...]
etymologywords.py -o etymology_words.sqlite --export res/wiktionary/etymology_words.tsv
"""
import argparse
import collections
import glob
import json
import os
import re
import sqlite3

RES = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'res', 'wiktionary')
REF = re.compile(r'<ref[^>/]*/>|<ref[^>]*>.*?</ref>|<[^>]+>', re.S)
TEMPLATE = re.compile(r'\{\{[^{}]*\}\}')
LINK = re.compile(r'\[\[[^\[\]]*\]\]')
ITALIC = re.compile(r"''+.*?''+")
QUOTED = re.compile(r'"[^"\n]*"|“[^”\n]*”|„[^”“\n]*[”“]|«[^»\n]*»|‘[^’\n]*’')
# a word: no space, digit or punctuation (marks of Indic scripts are no letters of \w), an
# abbreviation with its period ("vgl."); arrows and < as words of their own
TOKEN = re.compile(r"[^\s\d_.,;:!?()\[\]{}\"“”„«»|/=*#~<>+→←।॥၊။،؛]+\.?|[<>+→←]")
# the end of a sentence: a period before a capital (not of "vgl. A" … a guess), a semicolon, a line
SENTENCE = re.compile(r'(?<=\w{4})\.\s+(?=[A-ZÀ-ÖØ-ÞА-ЯΑ-Ω])|[;\n]')


def description(text):
    """the text of description of an etymology: no templates (inner first), links, italics, quotations"""
    text = REF.sub(' ', text)
    for _ in range(4):
        text, n = TEMPLATE.subn(' ', text)
        if not n:
            break
    for pattern in (LINK, ITALIC, QUOTED):
        text = pattern.sub(' ', text)
    return text


def phrases(text):
    """phrases of 1 to 3 words of each sentence, lowercase"""
    found = set()
    for sentence in SENTENCE.split(text):
        words = [w.lower() for w in TOKEN.findall(sentence)]
        for n in (1, 2, 3):
            for i in range(len(words) - n + 1):
                found.add(' '.join(words[i:i + n]))
    return found


def known(wiki):
    """phrase -> relation of the TSV and of the configuration of the wiki"""
    relations = {}
    path = os.path.join(RES, 'etymology_words.tsv')
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            for line in f:
                p = line.rstrip('\n').split('\t')
                if len(p) == 3 and p[0] == wiki:
                    relations[p[1].lower()] = p[2]
    try:
        with open(os.path.join(RES, wiki + '.json'), encoding='utf-8') as f:
            etymology = json.load(f).get('etymology', {})
    except (OSError, ValueError):
        etymology = {}
    for word, relation in etymology.get('words', {}).items():
        relations.setdefault(word.strip().lower(), relation)
    for word in etymology.get('doubt_words', []):
        relations.setdefault(word.strip().lower(), 'guess')
    return relations


def export(db, path):
    """the phrases given a relation into the TSV, by wiki and phrase"""
    rows = db.execute("SELECT wiki, phrase, relation FROM etymology_word WHERE relation IS NOT NULL AND "
                      "relation <> '' ORDER BY wiki, phrase").fetchall()
    with open(path, 'w', encoding='utf-8') as f:
        f.write('# Phrases of the etymologies of each wiki and their relation (tools/etymologywords.py):\n'
                '# from, borrowed, calque, compare, descendant, mention, unrelated, uncertain, guess (a word\n'
                '# of a guess: possibly), gloss (the next form a meaning: it "cioè"), none (a word of no\n'
                '# relation, reviewed).\n'
                'wiki\tphrase\trelation\n')
        for row in rows:
            f.write('\t'.join(row) + '\n')
    print(f'{len(rows)} phrases -> {path}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-i', default='uniform10', help='output of wikt2jsonl --uniform')
    parser.add_argument('-o', default='etymology_words.sqlite')
    parser.add_argument('--min', type=int, default=2, help='phrases of fewer etymologies left out')
    parser.add_argument('--show', type=int, default=20, help='phrases of no relation shown per wiki')
    parser.add_argument('--export', metavar='TSV', help='write the phrases of a relation into the TSV')
    parser.add_argument('wiki', nargs='*')
    args = parser.parse_args()
    db = sqlite3.connect(args.o)
    db.execute('CREATE TABLE IF NOT EXISTS etymology_word(wiki TEXT, phrase TEXT, etymologies INTEGER, '
               'example TEXT, relation TEXT, PRIMARY KEY(wiki, phrase))')
    if args.export:
        export(db, args.export)
        return
    for path in sorted(glob.glob(os.path.join(args.i, '*.jsonl'))):
        wiki = os.path.basename(path)[:-6]
        if args.wiki and wiki not in args.wiki:
            continue
        counts, examples, total = collections.Counter(), {}, 0
        with open(path, encoding='utf-8') as f:
            for line in f:
                for language in json.loads(line)['languages']:
                    for x in [language] + language.get('entries', []):
                        text = x.get('etymology', '')
                        if not text.strip() or x.get('etymology_unparsed') is None and 'etymology_chain' not in x:
                            continue  # (a placeholder of none)
                        total += 1
                        for p in phrases(description(text)):
                            counts[p] += 1
                            examples.setdefault(p, re.sub(r'\s+', ' ', text.strip())[:200])
        relations = known(wiki)
        for p, n in counts.items():
            if n < args.min and p not in relations:
                continue
            old = db.execute('SELECT relation FROM etymology_word WHERE wiki=? AND phrase=?', (wiki, p)).fetchone()
            relation = relations.get(p) or (old[0] if old else None)
            db.execute('INSERT OR REPLACE INTO etymology_word VALUES(?,?,?,?,?)', (wiki, p, n, examples[p], relation))
        db.commit()
        if total and args.show:
            rows = db.execute('SELECT phrase, etymologies FROM etymology_word WHERE wiki=? AND relation IS NULL '
                              'ORDER BY etymologies DESC, length(phrase) LIMIT ?', (wiki, args.show)).fetchall()
            print(f'{wiki}: {total} etymologies; no relation: ' + ', '.join(f'{p} {n}' for p, n in rows))


if __name__ == '__main__':
    main()
