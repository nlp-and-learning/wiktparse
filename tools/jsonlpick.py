#!/usr/bin/env python3
"""jsonlpick: the pages of a sample (wiktsamples: <samples>/<wiki>/pages.tsv) taken from a
block output (wikt2jsonl --dump, wiktlink, wiktglos: <wiki>.jsonl.zst + .idx + .dict) into
plain <out>/<wiki>.jsonl, in the order of the sample, for reading in the browser.

jsonlpick.py [-s samples100] [-i jsonl_senses] [-o jsonl100] [wiki...]
"""
import argparse
import csv
import os
import sys

import zstandard


def read_index(path):
    """[(offset, uncompressed, [(start, title)])], end of data"""
    blocks, end = [], None
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            if line.startswith('EOF='):
                end = int(line[4:])
            elif line.startswith(':'):
                start, title = line[1:].split(':', 1)
                blocks[-1][2].append((int(start), title))
            else:
                offset, _, uncompressed = line.split(':')
                blocks.append((int(offset), int(uncompressed), []))
    return blocks, end


def pick(wiki, samples, data, out):
    with open(os.path.join(samples, wiki, 'pages.tsv'), encoding='utf-8') as f:
        wanted = [r['title'] for r in csv.DictReader(f, delimiter='\t')]
    base = os.path.join(data, wiki + '.jsonl')
    blocks, end = read_index(base + '.idx')
    if end is None:
        end = os.path.getsize(base + '.zst')
    where = {}  # title -> block number (the first page of the title)
    for i, (_, _, pages) in enumerate(blocks):
        for _, title in pages:
            where.setdefault(title, i)
    dict_path = base + '.dict'
    dictionary = None
    if os.path.exists(dict_path) and os.path.getsize(dict_path):
        with open(dict_path, 'rb') as f:
            dictionary = zstandard.ZstdCompressionDict(f.read())
    decompressor = zstandard.ZstdDecompressor(dict_data=dictionary) if dictionary else zstandard.ZstdDecompressor()
    lines = {}
    with open(base + '.zst', 'rb') as f:
        for i in sorted({where[t] for t in wanted if t in where}):
            offset, uncompressed, pages = blocks[i]
            f.seek(offset)
            following = blocks[i + 1][0] if i + 1 < len(blocks) else end
            text = decompressor.decompress(f.read(following - offset), max_output_size=uncompressed)
            for k, (start, title) in enumerate(pages):
                stop = pages[k + 1][0] if k + 1 < len(pages) else len(text)
                lines.setdefault(title, text[start:stop].rstrip(b'\n'))
    found = [lines[t] for t in wanted if t in lines]
    with open(os.path.join(out, wiki + '.jsonl'), 'wb') as f:
        for line in found:
            f.write(line + b'\n')
    return len(found), len(wanted)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-s', default='samples100', help='sample directory (wiktsamples)')
    parser.add_argument('-i', default='jsonl_senses', help='block output to take the pages from')
    parser.add_argument('-o', default='jsonl100', help='output directory of plain <wiki>.jsonl')
    parser.add_argument('wiki', nargs='*')
    args = parser.parse_args()
    wikis = args.wiki or sorted(w for w in os.listdir(args.s) if os.path.exists(os.path.join(args.s, w, 'pages.tsv')))
    os.makedirs(args.o, exist_ok=True)
    total = [0, 0]
    for wiki in wikis:
        if not os.path.exists(os.path.join(args.i, wiki + '.jsonl.zst')):
            print(f'{wiki}: no {wiki}.jsonl.zst in {args.i}', file=sys.stderr)
            continue
        found, wanted = pick(wiki, args.s, args.i, args.o)
        total[0] += found
        total[1] += wanted
        if found < wanted:
            print(f'{wiki}: {found} of {wanted} pages', file=sys.stderr)
    print(f'{len(wikis)} wikis, {total[0]} of {total[1]} pages -> {args.o}')


if __name__ == '__main__':
    main()
