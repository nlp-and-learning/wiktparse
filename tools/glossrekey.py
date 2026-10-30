#!/usr/bin/env python3
"""glossrekey: the TSVs of links of glosses (glossembed.py --apply, glossllm.py: their chosen, voted,
minority files) keyed for the uniform tree. Their rows name a node of the tree of headings by its
path in the language section ("sections:1/children:0"), which the uniform tree has not; the part
of speech of that node (its section, "Noun") takes its place, read from the tree the TSV was made
of (wiktlink output, jsonl_link: only the blocks of the titles of the TSV, by its .idx).
wiktglos --extra reads both: a column "pos" for "path".

glossrekey.py [-i jsonl_link] [-o gloss_uniform] file.tsv...
  writes gloss_uniform/<the same name>: wiki title language pos gloss sense_id score margin accepted
"""
import argparse
import collections
import json
import os
import sys

import zstandard


def blocks_of(directory, wiki, titles):
    """the pages of these titles of <directory>/<wiki>.jsonl.zst: title -> page"""
    base = os.path.join(directory, wiki + '.jsonl')
    wanted = collections.defaultdict(list)  # block offset -> (start, title)
    block = None
    with open(base + '.idx', encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('#'):
                continue
            if line.startswith(':'):
                start, title = line[1:].split(':', 1)
                if title in titles:
                    wanted[block].append(int(start))
            elif line[:1].isdigit():
                block = int(line.split(':', 1)[0])
    offsets = sorted({int(line.split(':', 1)[0]) for line in open(base + '.idx', encoding='utf-8')
                      if line[:1].isdigit()})
    following = dict(zip(offsets, offsets[1:] + [os.path.getsize(base + '.zst')]))
    data = open(base + '.dict', 'rb').read()
    decompressor = zstandard.ZstdDecompressor(dict_data=zstandard.ZstdCompressionDict(data) if data else None)
    pages = {}
    with open(base + '.zst', 'rb') as f:
        for offset, starts in wanted.items():
            f.seek(offset)
            data = decompressor.decompressobj().decompress(f.read(following[offset] - offset))
            for start in starts:  # (a byte offset)
                end = data.find(b'\n', start)
                page = json.loads(data[start:end if end >= 0 else len(data)].decode('utf-8'))
                pages[page['title']] = page
    return pages


def node_at(language, path):
    """the node of a language section at a path "sections:1/children:0", else None"""
    node = language
    for step in path.split('/'):
        key, _, index = step.partition(':')
        items = node.get(key, [])
        if not index.isdigit() or int(index) >= len(items):
            return None
        node = items[int(index)]
    return node


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-i', default='jsonl_link', help='the tree of headings the TSVs were made of')
    parser.add_argument('-o', default='gloss_uniform')
    parser.add_argument('files', nargs='+')
    args = parser.parse_args()
    os.makedirs(args.o, exist_ok=True)
    for path in args.files:
        with open(path, encoding='utf-8') as f:
            header = f.readline().rstrip('\n').split('\t')
            rows = [line.rstrip('\n').split('\t') for line in f if line.strip()]
        if len(header) < 4 or header[3] != 'path':
            print(f'{path}: no column "path", left out', file=sys.stderr)
            continue
        by_wiki = collections.defaultdict(set)
        for r in rows:
            by_wiki[r[0]].add(r[1])
        pages = {}
        for wiki, titles in by_wiki.items():
            pages[wiki] = blocks_of(args.i, wiki, titles)
        out, lost = [], 0
        for r in rows:
            wiki, title, code, where = r[:4]
            page = pages[wiki].get(title)
            node = None
            for language in (page or {}).get('sections', []):
                if str(language.get('code')) == code and (node := node_at(language, where)) is not None:
                    break
            if node is None or not node.get('section'):
                lost += 1
                continue
            out.append([wiki, title, code, node['section']] + r[4:])
        name = os.path.join(args.o, os.path.basename(path))
        with open(name, 'w', encoding='utf-8') as f:
            f.write('\t'.join(header[:3] + ['pos'] + header[4:]) + '\n')
            for r in out:
                f.write('\t'.join(r) + '\n')
        print(f'{path}: {len(out)} rows -> {name}' + (f', {lost} of no node left out' if lost else ''))


if __name__ == '__main__':
    main()
