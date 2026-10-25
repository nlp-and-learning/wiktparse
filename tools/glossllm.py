#!/usr/bin/env python3
"""Glosses of translation tables linked to definitions by an LLM (HANDOFF-3, stage 5).

One request per entry: its numbered definitions and the glosses to link; the answer is JSON
(a definition id or none, and a confidence) by the schema given to Ollama (local models, and
cloud ones such as glm-4.7:cloud by the same API).

  glossllm.py --eval  --gold ../res/defglos/gold-en.json --model qwen3:14b [-j 4]
      accuracy on the reviewed links (a subsense or its parent counts), none included
  glossllm.py --apply [-i jsonl_senses] [--wiki en] --model qwen3:14b [-o glossllm-en.tsv]
      the glosses without a link, and those of best and embedding: a TSV like glossembed.py
      (accepted from the confidence), for wiktglos --extra
"""
import argparse
import concurrent.futures
import json
import os
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glossembed as ge  # noqa: E402

SYSTEM = ('You link the glosses of translation tables of a Wiktionary entry to its definitions. '
          'A gloss names one sense briefly; answer with the id of the definition it names, or null when '
          'no definition is that sense. A subsense (1.2) is right when the gloss is that narrower sense, '
          'its parent (1) when the gloss covers the whole. Nearly every gloss has its definition: null only '
          'when it surely has none. Answer only the JSON, as '
          '{"links": [{"gloss": 0, "definition": "<id>", "confidence": 0.9}]}')

SCHEMA = {'type': 'object', 'properties': {'links': {'type': 'array', 'items': {
    'type': 'object', 'properties': {'gloss': {'type': 'integer'}, 'definition': {'type': ['string', 'null']},
                                     'confidence': {'type': 'number'}},
    'required': ['gloss', 'definition', 'confidence']}}}, 'required': ['links']}


def schema(defs, glosses):
    """The answer of an entry: only its definition ids or null (a free string runs away)"""
    return {'type': 'object', 'properties': {'links': {'type': 'array', 'items': {
        'type': 'object', 'properties': {'gloss': {'type': 'integer', 'enum': list(range(len(glosses)))},
                                         'definition': {'enum': [i for i, text in defs if text.strip(' .')] + [None]},
                                         'confidence': {'type': 'number'}},
        'required': ['gloss', 'definition', 'confidence']}}}, 'required': ['links']}


def prompt(title, pos, defs, glosses):
    lines = [f'Entry: {title} ({pos})', 'Definitions:']
    lines += [f'  {i}: {text[:300]}' for i, text in defs if text.strip(' .')]
    lines += ['Glosses to link:'] + [f'  [{k}] {ge.plain(g)}' for k, g in enumerate(glosses)]
    return '\n'.join(lines)


def ask(model, title, pos, defs, glosses, url='http://localhost:11434'):
    """{gloss index: (definition id or None, confidence)}"""
    body = {'model': model, 'stream': False, 'format': schema(defs, glosses),
            # (a limit: JSON by a schema may run on without an end, spaces or repeated items)
            'options': {'temperature': 0.2, 'num_ctx': 4096, 'num_predict': 60 + 40 * len(glosses)},
            'messages': [{'role': 'system', 'content': SYSTEM},
                         {'role': 'user', 'content': prompt(title, pos, defs, glosses)}]}
    if model.startswith(('qwen3', 'gpt-oss', 'deepseek-r1')):
        body['think'] = 'low' if model.startswith('gpt-oss') else False
    request = urllib.request.Request(url + '/api/chat', json.dumps(body).encode(), {'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, timeout=300) as r:
        content = json.load(r)['message']['content']
    ids = {i for i, _ in defs}
    result = {}
    try:
        for link in json.loads(content).get('links', []):
            k, d = link.get('gloss'), link.get('definition')
            if d is not None:  # "id:4", "4.", " 1.2 "
                d = str(d).strip().removeprefix('id:').removeprefix('id ').strip().rstrip('.')
            if isinstance(k, int) and 0 <= k < len(glosses):
                result[k] = (d if d in ids else None, float(link.get('confidence') or 0))
    except (json.JSONDecodeError, AttributeError, TypeError, ValueError):
        pass
    return result


def run_eval(args):
    gold = json.load(open(args.gold, encoding='utf-8'))
    # One request per entry: the reviewed glosses of it
    entries = {}
    for item in gold:
        key = (item['title'], item['pos'])
        entries.setdefault(key, {'defs': item['defs'], 'glosses': [], 'gold': []})
        entries[key]['glosses'].append(item['gloss'])
        entries[key]['gold'].append(item['gold'])
    if args.limit:
        entries = dict(list(entries.items())[:args.limit])
    started = time.time()
    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(ask, args.model, t, p, e['defs'], e['glosses']): e for (t, p), e in entries.items()}
        for done, future in enumerate(concurrent.futures.as_completed(futures), 1):
            print(f'\r  {done}/{len(futures)} entries', end='', file=sys.stderr, flush=True)
            e = futures[future]
            try:
                answer = future.result()
            except Exception as error:  # a failed request: all its glosses unanswered
                print(f'  error: {error}', file=sys.stderr)
                answer = {}
            for k, expected in enumerate(e['gold']):
                got, confidence = answer.get(k, (None, 0.0))
                right = (got is None and not expected) or (got is not None and any(ge.related(got, x) for x in expected))
                results.append((confidence, right, got is None))
    print(file=sys.stderr)
    seconds = time.time() - started
    n = len(results)
    print(f'{args.model}: {n} glosses of {len(entries)} entries in {seconds:.0f} s ({seconds / len(entries):.1f} s/entry); '
          f'accuracy {sum(r[1] for r in results) / n:.3f}, none answered {sum(r[2] for r in results)}')
    for threshold in (0.5, 0.7, 0.8, 0.9):
        kept = [r for r in results if r[0] >= threshold and not r[2]]
        if kept:
            print(f'  confidence >= {threshold}: {len(kept) / n:.1%} kept, accuracy {sum(r[1] for r in kept) / len(kept):.3f}')


def run_apply(args):
    cases = []
    for page in ge.pages(args.input, args.wiki):
        for code, path, node, tables in ge.entries(page):
            defs, glosses = ge.entry_data(node, tables)
            todo = [g for g, (ids, method, score) in glosses.items() if not ids or method in ('best', 'embedding')]
            if todo and len(defs) >= 2:
                cases.append((page['title'], code, '/'.join(f'{k}:{i}' for k, i in path), node.get('section'), defs, todo,
                              {g: glosses[g] for g in todo}))
    print(f'{len(cases)} entries, {sum(len(c[5]) for c in cases)} glosses', file=sys.stderr)
    started, done = time.time(), 0
    with open(args.output, 'w', encoding='utf-8') as out, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        out.write('wiki\ttitle\tlanguage\tpath\tgloss\tsense_id\tscore\tmargin\taccepted\n')
        futures = {pool.submit(ask, args.model, title, pos, defs, todo): (title, code, where, todo, before)
                   for title, code, where, pos, defs, todo, before in cases}
        for future in concurrent.futures.as_completed(futures):
            title, code, where, todo, before = futures[future]
            try:
                answer = future.result()
            except Exception as error:
                print(f'  error {title}: {error}', file=sys.stderr)
                continue
            for k, gloss in enumerate(todo):
                got, confidence = answer.get(k, (None, 0.0))
                if got is None:
                    continue
                accepted = int(confidence >= args.confidence)
                out.write(f'{args.wiki}\t{title}\t{code}\t{where}\t{gloss}\t{got}\t{confidence:.2f}\t\t{accepted}\n')
            done += 1
            if done % 100 == 0:
                rate = done / (time.time() - started)
                print(f'\r  {done}/{len(cases)} entries, {rate:.1f}/s', end='', file=sys.stderr, flush=True)
    print(f'\nwritten to {args.output}', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--eval', action='store_true')
    mode.add_argument('--apply', action='store_true')
    parser.add_argument('--gold', default='../res/defglos/gold-en.json')
    parser.add_argument('-i', '--input', default='jsonl_senses')
    parser.add_argument('-o', '--output')
    parser.add_argument('--wiki', default='en')
    parser.add_argument('--model', default='qwen3:14b')
    parser.add_argument('--confidence', type=float, default=0.8, help='accepted from this confidence')
    parser.add_argument('-j', '--jobs', type=int, default=4, help='requests at a time (OLLAMA_NUM_PARALLEL)')
    parser.add_argument('--limit', type=int, help='--eval: the first entries only')
    args = parser.parse_args()
    args.output = args.output or f'glossllm-{args.wiki}.tsv'
    (run_eval if args.eval else run_apply)(args)


if __name__ == '__main__':
    main()
