#!/usr/bin/env python3
"""Glosses of translation tables linked to definitions by an LLM (HANDOFF-3, stage 5).

One request per entry: its numbered definitions and the glosses to link; the answer is JSON
(a definition id or none, and a confidence) by the schema given to Ollama (local models), or
as JSON from cometapi:glm-4.7 (COMETAPI_KEY), zai:glm-4.7 (ZAI_API_KEY), anthropic:<model>.

  glossllm.py --eval  --gold ../res/defglos/gold-en.json --model qwen3:14b [-j 4]
      accuracy on the reviewed links (a subsense or its parent counts), none included;
      --fallback MODEL: the answers below --confidence again by a second model (which may say null)
  glossllm.py --apply [-i jsonl_senses] [--wiki en] --model qwen3:14b [-o glossllm-en.tsv]
      the glosses without a link (--recheck: also those of best and embedding): a TSV like
      glossembed.py (accepted from the confidence), for wiktglos --extra. The local models make
      some 10% errors on them (gold-en-hard.json): meant for a stronger model.
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


SYSTEM_FALLBACK = SYSTEM.replace('Nearly every gloss has its definition: null only when it surely has none.',
                                 'Answer null when no definition is the sense of the gloss.')


def ask_anthropic(model, system, user, glosses):
    """The answer text of an Anthropic model (anthropic:claude-haiku-5-5), ANTHROPIC_API_KEY"""
    body = {'model': model, 'max_tokens': 100 + 60 * len(glosses), 'temperature': 0.2, 'system': system,
            'messages': [{'role': 'user', 'content': user}]}
    request = urllib.request.Request('https://api.anthropic.com/v1/messages', json.dumps(body).encode(),
                                     {'Content-Type': 'application/json', 'anthropic-version': '2023-06-01',
                                      'x-api-key': os.environ['ANTHROPIC_API_KEY']})
    with urllib.request.urlopen(request, timeout=120) as r:
        text = json.load(r)['content'][0]['text']
    return text[text.find('{'):text.rfind('}') + 1]


# OpenAI-compatible providers: <prefix>:<model>, the key from the environment
PROVIDERS = {'cometapi': ('https://api.cometapi.com/v1/chat/completions', 'COMETAPI_KEY'),
             'zai': ('https://api.z.ai/api/paas/v4/chat/completions', 'ZAI_API_KEY')}


def ask_openai(provider, model, system, user, glosses):
    """The answer text of an OpenAI-compatible provider (cometapi:glm-4.7, zai:glm-4.7)"""
    url, key = PROVIDERS[provider]
    if not os.environ.get(key):
        raise RuntimeError(f'{key} is not set')
    body = {'model': model, 'temperature': 0.2, 'max_tokens': 200 + 60 * len(glosses),
            'response_format': {'type': 'json_object'},
            'messages': [{'role': 'system', 'content': system}, {'role': 'user', 'content': user}]}
    if model.startswith('glm'):
        body['thinking'] = {'type': 'disabled'}  # GLM 4.5+: no reasoning, a short answer
    request = urllib.request.Request(url, json.dumps(body).encode(),
                                     {'Content-Type': 'application/json', 'Authorization': f'Bearer {os.environ[key]}'})
    with urllib.request.urlopen(request, timeout=120) as r:
        text = json.load(r)['choices'][0]['message']['content'] or ''
    return text[text.find('{'):text.rfind('}') + 1]


def ask(model, title, pos, defs, glosses, url='http://localhost:11434', fallback=False):
    """{gloss index: (definition id or None, confidence)}; fallback: a second model, which may
    answer null"""
    system = SYSTEM_FALLBACK if fallback else SYSTEM
    if model.startswith('anthropic:'):
        content = ask_anthropic(model.removeprefix('anthropic:'), system, prompt(title, pos, defs, glosses), glosses)
        return parse(content, defs, glosses)
    if model.split(':', 1)[0] in PROVIDERS:
        provider, name = model.split(':', 1)
        return parse(ask_openai(provider, name, system, prompt(title, pos, defs, glosses), glosses), defs, glosses)
    body = {'model': model, 'stream': False, 'format': schema(defs, glosses),
            # (a limit: JSON by a schema may run on without an end, spaces or repeated items)
            'options': {'temperature': 0.2, 'num_ctx': 4096, 'num_predict': 60 + 40 * len(glosses)},
            'messages': [{'role': 'system', 'content': system},
                         {'role': 'user', 'content': prompt(title, pos, defs, glosses)}]}
    if model.startswith(('qwen3', 'gpt-oss', 'deepseek-r1')):
        body['think'] = 'low' if model.startswith('gpt-oss') else False
    request = urllib.request.Request(url + '/api/chat', json.dumps(body).encode(), {'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, timeout=300) as r:
        content = json.load(r)['message']['content']
    return parse(content, defs, glosses)


def parse(content, defs, glosses):
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


def ask_cascade(args, title, pos, defs, glosses):
    """The main model; the glosses it is unsure of (below --confidence) to --fallback"""
    answer = ask(args.model, title, pos, defs, glosses)
    if not args.fallback:
        return answer
    unsure = [k for k in range(len(glosses)) if answer.get(k, (None, 0.0))[1] < args.confidence or answer.get(k, (None,))[0] is None]
    if unsure:
        second = ask(args.fallback, title, pos, defs, [glosses[k] for k in unsure], fallback=True)
        for j, k in enumerate(unsure):
            if j in second:
                got, confidence = second[j]
                answer[k] = (got, max(confidence, args.confidence) if got is not None else confidence)
    return answer


SURE = ('positional', 'single', 'number')


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
        futures = {pool.submit(ask_cascade, args, t, p, e['defs'], e['glosses']): e for (t, p), e in entries.items()}
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
                results.append((confidence, right, got is None, not expected))
    print(file=sys.stderr)
    seconds = time.time() - started
    n = len(results)
    print(f'{args.model}: {n} glosses of {len(entries)} entries in {seconds:.0f} s ({seconds / len(entries):.1f} s/entry); '
          f'accuracy {sum(r[1] for r in results) / n:.3f}, none answered {sum(r[2] for r in results)}, '
          f'none expected {sum(r[3] for r in results)} of them right {sum(r[1] and r[3] for r in results)}')
    for threshold in (0.5, 0.7, 0.8, 0.9):
        kept = [r for r in results if r[0] >= threshold and not r[2]]
        if kept:
            print(f'  confidence >= {threshold}: {len(kept) / n:.1%} kept, accuracy {sum(r[1] for r in kept) / len(kept):.3f}')


def run_apply(args):
    cases = []
    for page in ge.pages(args.input, args.wiki):
        for code, path, node, tables in ge.entries(page):
            defs, glosses = ge.entry_data(node, tables)
            todo = [g for g, (ids, method, score) in glosses.items()
                    if not ids or (args.recheck and method in ('best', 'embedding'))]
            # The definitions the easy methods left: not those surely of other glosses (a shorter
            # request, fewer to confuse); all of them when none is left
            taken = {i for g, (ids, method, score) in glosses.items() if g not in todo and method in SURE for i in ids or []}
            free = [(i, text) for i, text in defs if i not in taken]
            if args.free and len(free) >= 1:
                defs = free
            if todo and len(defs) >= (1 if args.free else 2):
                cases.append((page['title'], code, '/'.join(f'{k}:{i}' for k, i in path), node.get('section'), defs, todo,
                              {g: glosses[g] for g in todo}))
    print(f'{len(cases)} entries, {sum(len(c[5]) for c in cases)} glosses', file=sys.stderr)
    started, done = time.time(), 0
    with open(args.output, 'w', encoding='utf-8') as out, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        out.write('wiki\ttitle\tlanguage\tpath\tgloss\tsense_id\tscore\tmargin\taccepted\n')
        futures = {pool.submit(ask_cascade, args, title, pos, defs, todo): (title, code, where, todo, before)
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
    parser.add_argument('--fallback', help='a second model for the unsure answers: cometapi:glm-4.7 (COMETAPI_KEY), '
                                           'zai:glm-4.7 (ZAI_API_KEY), anthropic:claude-haiku-5-5 (ANTHROPIC_API_KEY), '
                                           'or an Ollama model')
    parser.add_argument('--confidence', type=float, default=0.8, help='accepted from this confidence')
    parser.add_argument('-j', '--jobs', type=int, default=4, help='requests at a time (OLLAMA_NUM_PARALLEL)')
    parser.add_argument('--limit', type=int, help='--eval: the first entries only')
    parser.add_argument('--recheck', action='store_true',
                        help='--apply: also the glosses linked by best and embedding (some 10%% of them wrong)')
    parser.add_argument('--all-definitions', dest='free', action='store_false',
                        help='--apply: every definition of an entry, also those surely of other glosses')
    args = parser.parse_args()
    args.output = args.output or f'glossllm-{args.wiki}.tsv'
    for model in filter(None, (args.model, args.fallback)):  # a missing key: now, not per request
        prefix = model.split(':', 1)[0]
        key = PROVIDERS[prefix][1] if prefix in PROVIDERS else 'ANTHROPIC_API_KEY' if prefix == 'anthropic' else None
        if key and not os.environ.get(key):
            sys.exit(f'{model}: {key} is not set')
    (run_eval if args.eval else run_apply)(args)


if __name__ == '__main__':
    main()
