#!/usr/bin/env python3
"""Glosses of translation tables linked to definitions by an LLM (HANDOFF-3, stage 5).

One request per entry: its numbered definitions and the glosses to link; the answer is JSON
(a definition id or none, and a confidence) by the schema given to Ollama (local models), or
as JSON from cometapi:glm-4.7 (COMETAPI_KEY), zai:glm-4.7 (ZAI_API_KEY), anthropic:<model>.

  glossllm.py --eval  --gold ../res/defglos/gold-en.json --model qwen3:14b [-j 4]
      accuracy on the reviewed links (a subsense or its parent counts), none included;
      --fallback MODEL: the answers below --confidence again by a second model (which may say null);
      --verify MODEL: each answer checked by a second model, only confirmed ones kept
  glossllm.py --agree glossllm-en.tsv glossllm-en-qwen.tsv
      the links both models (--apply of each, -o glossllm-en-qwen.tsv for the second) agree on:
      glossllm-en.agreed.tsv, the others in glossllm-en.agreed.txt (40 en links: air alone 4 wrong;
      agreed with qwen3:14b 2 wrong, 3 right ones left out; checked by glm-4.7 2 wrong, 6 left out)
  glossllm.py --check glossllm-en.tsv --verify zai:glm-4.7 [--wiki en]
      the accepted links of a TSV confirmed: glossllm-en.checked.tsv (accepted only the confirmed),
      glossllm-en.checked.txt (the other answers), in parts like --apply
  glossllm.py --apply [-i jsonl_senses] [--wiki en] --model qwen3:14b [-o glossllm-en.tsv]
      the glosses without a link (--recheck: also those of best and embedding): a TSV like
      glossembed.py (accepted from the confidence), for wiktglos --extra. The local models make
      some 10% errors on them (gold-en-hard.json): meant for a stronger model.
"""
import argparse
import concurrent.futures
import csv
import json
import os
import re
import sys
import time
import urllib.error
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


def examples(node):
    """The first usage example of each sense of a node (id -> text), as words"""
    found = {}
    for holder in [node] + [c for c in node.get('children', []) if c.get('section') == node.get('section')]:
        for s in holder.get('senses', []):
            if s.get('examples'):
                found[s['id']] = ge.plain(s['examples'][0])[:100]
    return found


def prompt(title, pos, defs, glosses, notes=None):
    """notes: per definition id, words after it (an example, the glosses it already has)"""
    notes = notes or {}
    lines = [f'Entry: {title} ({pos})', 'Definitions:']
    lines += [f'  {i}: {text[:300]}' + (f'  [{notes[i]}]' if i in notes else '') for i, text in defs if text.strip(' .')]
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


def load_env(path='.env'):
    """KEY=VALUE lines of .env into the environment, for keys not set (ZAI_API_KEY)"""
    if os.path.exists(path):
        for line in open(path, encoding='utf-8'):
            line = line.strip().removeprefix('export ').strip()
            if '=' in line and not line.startswith('#'):
                key, value = line.split('=', 1)
                os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


TOKENS = {'in': 0, 'out': 0}  # of the provider's answers, for the cost
TOKENS_LOCK = __import__('threading').Lock()


class OutOfCredit(Exception):
    """The provider refuses for money or limits: the run stops, its results kept"""


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
    for attempt in range(8):
        try:
            with urllib.request.urlopen(request, timeout=120) as r:
                answer = json.load(r)
            text = answer['choices'][0]['message']['content'] or ''
            with TOKENS_LOCK:
                TOKENS['in'] += answer.get('usage', {}).get('prompt_tokens', 0)
                TOKENS['out'] += answer.get('usage', {}).get('completion_tokens', 0)
            return text[text.find('{'):text.rfind('}') + 1]
        except urllib.error.HTTPError as e:
            detail = e.read().decode(errors='replace')[:300]
            low = detail.lower()
            # Money (z.ai 1113 insufficient balance, 402): stop; a rate limit (z.ai 1302, 429): wait
            if e.code in (401, 402, 403) or any(w in low for w in ('balance', 'insufficient', 'arrear', 'credit', '"1113"')):
                raise OutOfCredit(f'{provider}: HTTP {e.code} {detail}') from e
            if e.code == 429 or 'rate limit' in low:
                time.sleep(min(60, 2 ** attempt))
                continue
            raise
    raise RuntimeError(f'{provider}: still rate limited')


def ask(model, title, pos, defs, glosses, url='http://localhost:11434', fallback=False, notes=None):
    """{gloss index: (definition id or None, confidence)}; fallback: a second model, which may
    answer null"""
    system = SYSTEM_FALLBACK if fallback else SYSTEM
    if model.startswith('anthropic:'):
        content = ask_anthropic(model.removeprefix('anthropic:'), system, prompt(title, pos, defs, glosses, notes), glosses)
        return parse(content, defs, glosses)
    if model.split(':', 1)[0] in PROVIDERS:
        provider, name = model.split(':', 1)
        return parse(ask_openai(provider, name, system, prompt(title, pos, defs, glosses, notes), glosses), defs, glosses)
    body = {'model': model, 'stream': False, 'format': schema(defs, glosses),
            # (a limit: JSON by a schema may run on without an end, spaces or repeated items)
            'options': {'temperature': 0.2, 'num_ctx': 4096, 'num_predict': 60 + 40 * len(glosses)},
            'messages': [{'role': 'system', 'content': system},
                         {'role': 'user', 'content': prompt(title, pos, defs, glosses, notes)}]}
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


SYSTEM_VERIFY = ('You check links of translation-table glosses of a Wiktionary entry to its definitions, made '
                 'by another model. For each gloss you see the definition it was linked to. Answer the id of '
                 'the definition the gloss really names: the same id when the link is right, another id when '
                 'another definition is the sense, null when no definition is. Answer only the JSON, as '
                 '{"checks": [{"gloss": 0, "definition": "<id>"}]}')


def verify(model, title, pos, defs, glosses, proposed):
    """A second model checks links: for each gloss index, the definition it gives (the
    proposed one: confirmed; another: a different answer; None: no definition)"""
    texts = dict(defs)
    lines = [prompt(title, pos, defs, [], None).replace('Glosses to link:', 'Links to check:')]
    lines += [f'  [{k}] «{ge.plain(g)}» linked to {p}: {texts.get(p, "")[:200]}' for k, (g, p) in enumerate(zip(glosses, proposed))]
    provider, name = model.split(':', 1)
    content = ask_openai(provider, name, SYSTEM_VERIFY, '\n'.join(lines), glosses)
    ids, result = {i for i, _ in defs}, {}
    try:
        for check in json.loads(content).get('checks', []):
            k, d = check.get('gloss'), check.get('definition')
            if isinstance(k, int) and 0 <= k < len(glosses):
                d = None if d is None else str(d).strip().removeprefix('id:').strip().rstrip('.')
                result[k] = d if d in ids else None
    except (json.JSONDecodeError, AttributeError, TypeError, ValueError):
        pass
    return result


SYSTEM_BATCH = SYSTEM.replace(
    'Answer only the JSON, as {"links": [{"gloss": 0, "definition": "<id>", "confidence": 0.9}]}',
    'Each entry (E0, E1, …) is separate: link its glosses to its own definitions only. Answer only the JSON, '
    'one item per gloss, as {"links": [{"entry": 0, "gloss": 0, "definition": "<id>", "confidence": 0.9}]}')


def ask_batch(model, items):
    """Several entries in one request (a provider's limit is of requests, not tokens):
    items (title, pos, defs, glosses, notes); for each, {gloss index: (id or None, confidence)}"""
    parts = []
    for n, (title, pos, defs, glosses, notes) in enumerate(items):
        text = prompt(title, pos, defs, glosses, notes).replace('Entry: ', f'E{n}. Entry: ', 1)
        parts.append(text.replace('Glosses to link:', f'Glosses of E{n} to link:'))
    user = '\n\n'.join(parts)
    count = sum(len(item[3]) for item in items)
    if model.startswith('anthropic:'):
        content = ask_anthropic(model.removeprefix('anthropic:'), SYSTEM_BATCH, user, [None] * count)
    else:
        provider, name = model.split(':', 1)
        content = ask_openai(provider, name, SYSTEM_BATCH, user, [None] * count)
    results = [{} for _ in items]
    try:
        for link in json.loads(content).get('links', []):
            n = link.get('entry')
            if isinstance(n, int) and 0 <= n < len(items):
                title, pos, defs, glosses, notes = items[n]
                one = parse(json.dumps({'links': [{k: v for k, v in link.items() if k != 'entry'}]}), defs, glosses)
                results[n].update(one)
    except (json.JSONDecodeError, AttributeError, TypeError, ValueError):
        pass
    return results


def batched(args):
    """Whether entries go several to a request: API providers with --batch above 1"""
    prefix = args.model.split(':', 1)[0]
    return args.batch > 1 and (prefix in PROVIDERS or prefix == 'anthropic')


def ask_cascade(args, title, pos, defs, glosses, notes=None):
    """The main model; the glosses it is unsure of (below --confidence, or null) to --fallback.
    With a fallback the main model may say null (a cheap model leaves the hard ones)"""
    answer = ask(args.model, title, pos, defs, glosses, notes=notes, fallback=bool(args.fallback))
    if not args.fallback:
        return answer
    unsure = [k for k in range(len(glosses)) if answer.get(k, (None, 0.0))[1] < args.confidence or answer.get(k, (None,))[0] is None]
    if unsure:
        second = ask(args.fallback, title, pos, defs, [glosses[k] for k in unsure], fallback=True, notes=notes)
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
    items = [(t, p, e['defs'], e['glosses'], None, e) for (t, p), e in entries.items()]
    groups = [items[i:i + args.batch] for i in range(0, len(items), args.batch)] if batched(args) else [[x] for x in items]

    def run(group):
        if len(group) > 1 or batched(args):
            return ask_batch(args.model, [g[:5] for g in group])
        t, p, defs, glosses, notes, e = group[0]
        return [ask_cascade(args, t, p, defs, glosses)]

    answers = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(run, group): group for group in groups}
        try:
            completed = list(enumerate(concurrent.futures.as_completed(futures), 1))
        except KeyboardInterrupt:
            pool.shutdown(wait=False, cancel_futures=True)
            sys.exit('interrupted')
        for done, future in completed:
            print(f'\r  {done}/{len(futures)} requests', end='', file=sys.stderr, flush=True)
            group = futures[future]
            try:
                got = future.result()
            except Exception as error:  # a failed request: all its glosses unanswered
                print(f'  error: {error}', file=sys.stderr)
                got = [{} for _ in group]
            answers += [(g[5], a, g) for g, a in zip(group, got)]
    # --verify: a second model confirms each answer (kept), gives another (not kept), or none
    verdicts = {}
    if args.verify:
        def check(item):
            e, answer, g = item
            ks = [k for k in range(len(e['gold'])) if answer.get(k, (None,))[0] is not None]
            if not ks:
                return id(e), {}
            v = verify(args.verify, g[0], g[1], g[2], [g[3][k] for k in ks], [answer[k][0] for k in ks])
            return id(e), {k: v.get(j) for j, k in enumerate(ks)}
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            for key, v in pool.map(check, answers):
                verdicts[key] = v
    other_right = other = 0
    for e, answer, g in answers:
        for k, expected in enumerate(e['gold']):
            got, confidence = answer.get(k, (None, 0.0))
            if args.verify and got is not None:
                second = verdicts.get(id(e), {}).get(k)
                if second != got:  # not confirmed: no link (another answer counted apart)
                    if second is not None:
                        other += 1
                        other_right += any(ge.related(second, x) for x in expected)
                    got, confidence = None, 0.0
            right = (got is None and not expected) or (got is not None and any(ge.related(got, x) for x in expected))
            results.append((confidence, right, got is None, not expected))
    if args.verify:
        print(f'  verify {args.verify}: another definition given {other} times, right {other_right}', file=sys.stderr)
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
    """In parts: the entries done (also those without an answer) in <output>.done, the links
    appended to <output>; --limit entries per run; stops when the provider refuses for money"""
    done_path = args.output + '.done'
    done = set(open(done_path, encoding='utf-8').read().splitlines()) if os.path.exists(done_path) else set()
    cases = []
    for page in ge.pages(args.input, args.wiki):
        for code, path, node, tables in ge.entries(page):
            defs, glosses = ge.entry_data(node, tables)
            todo = [g for g, (ids, method, score) in glosses.items()
                    if not ids or (args.recheck and method in ('best', 'embedding'))]
            # Every definition, with its first example and the glosses the easy methods surely gave
            # it (a gloss may be of a definition that has another: en "it", calculator); --free-only:
            # only the definitions without such glosses (shorter, but those glosses lost)
            notes = examples(node) if args.examples else {}
            taken = {}
            for g, (ids, method, score) in glosses.items():
                if g not in todo and method in SURE:
                    for i in ids or []:
                        taken.setdefault(i, []).append(g)
            for i, gs in taken.items():
                notes[i] = (notes[i] + '; ' if i in notes else '') + 'linked: ' + ', '.join(gs)[:120]
            if args.free_only:
                defs = [(i, text) for i, text in defs if i not in taken] or defs
            where = '/'.join(f'{k}:{i}' for k, i in path)
            if todo and len(defs) >= 2 and f"{page['title']}\t{code}\t{where}" not in done:
                cases.append((page['title'], code, where, node.get('section'), defs, todo, notes))
    total = len(cases)
    if args.limit:
        cases = cases[:args.limit]
    print(f'{total} entries to do ({len(done)} done before), now {len(cases)}', file=sys.stderr)
    new_file = not os.path.exists(args.output)
    started, finished, links, stopped = time.time(), 0, 0, None
    review_path = args.output.removesuffix('.tsv') + '.txt'
    with open(args.output, 'a', encoding='utf-8') as out, open(done_path, 'a', encoding='utf-8') as done_file, \
            open(review_path, 'a', encoding='utf-8') as review, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        if new_file:
            out.write('wiki\ttitle\tlanguage\tpath\tgloss\tsense_id\tscore\tmargin\taccepted\n')
        # Several entries to a request (--batch) for API providers, one by one for Ollama
        size = args.batch if batched(args) else 1
        groups = [cases[i:i + size] for i in range(0, len(cases), size)]

        def run(group):
            if size > 1:
                return ask_batch(args.model, [(t, p, d, todo, n) for t, c, w, p, d, todo, n in group])
            t, c, w, p, d, todo, n = group[0]
            return [ask_cascade(args, t, p, d, todo, n)]

        futures = {pool.submit(run, group): group for group in groups}

        def results():
            for future in concurrent.futures.as_completed(futures):
                group = futures[future]
                try:
                    got = future.result()
                except OutOfCredit:
                    raise
                except Exception as error:  # these entries again in the next run
                    print(f'\n  error {group[0][0]}: {error}', file=sys.stderr)
                    continue
                for case, answer in zip(group, got):
                    yield case, answer

        stream = results()
        while True:
            try:
                (title, code, where, pos, defs, todo, notes), answer = next(stream)
            except StopIteration:
                break
            except (OutOfCredit, KeyboardInterrupt) as e:
                # Ctrl+C or no money: the queued requests cancelled, the running ones (-j) ended
                stopped = str(e) or 'interrupted'
                print(f'\n  stopping: the {args.jobs} requests running end, the rest is left for the next run',
                      file=sys.stderr)
                pool.shutdown(wait=False, cancel_futures=True)
                break
            # For review before wiktglos --extra: the entry, each gloss with its definition
            texts = dict(defs)
            review.write(f'== {title} ({pos}) [{code} {where}]\n')
            for k, gloss in enumerate(todo):
                got, confidence = answer.get(k, (None, 0.0))
                review.write(f'  «{gloss}» -> {got or "none"} ({confidence:.2f})'
                             + (f'  {texts.get(got, "")[:150]}' if got else '') + '\n')
                if got is not None:
                    out.write(f'{args.wiki}\t{title}\t{code}\t{where}\t{gloss}\t{got}\t{confidence:.2f}\t\t'
                              f'{int(confidence >= args.confidence)}\n')
                    links += 1
            done_file.write(f'{title}\t{code}\t{where}\n')
            finished += 1
            if finished % 10 == 0 or finished == len(cases):
                out.flush(), done_file.flush(), review.flush()
                rate = finished / (time.time() - started)
                left = (len(cases) - finished) / rate if rate else 0
                print(f'\r  {finished}/{len(cases)} entries ({100 * finished / len(cases):.0f}%), {rate:.2f}/s, '
                      f'{int(left // 3600)}:{int(left % 3600 // 60):02d}:{int(left % 60):02d} left, '
                      f'tokens in {TOKENS["in"]} out {TOKENS["out"]}   ', end='', file=sys.stderr, flush=True)
    print(f'\n{finished} entries done, {links} links appended to {args.output} (to review: {review_path}); '
          f'{total - finished} entries left', file=sys.stderr)
    if stopped:
        sys.exit(f'stopped: {stopped}')


def run_check(args):
    """The links of --check <glossllm-<wiki>.tsv> confirmed by --verify: <name>.checked.tsv (accepted
    only the confirmed), <name>.checked.txt (the other answers, to review); in parts like --apply"""
    base = args.check.removesuffix('.tsv')
    out_path, review_path, done_path = base + '.checked.tsv', base + '.checked.txt', base + '.checked.done'
    done = set(open(done_path, encoding='utf-8').read().splitlines()) if os.path.exists(done_path) else set()
    links = {}
    for row in csv.DictReader(open(args.check, encoding='utf-8'), delimiter='\t'):
        if row['accepted'] == '1':
            links.setdefault((row['title'], row['language'], row['path']), []).append(row)
    cases = []
    for page in ge.pages(args.input, args.wiki):
        for code, path, node, tables in ge.entries(page):
            key = (page['title'], code, '/'.join(f'{k}:{i}' for k, i in path))
            if key in links and '\t'.join(key) not in done:
                defs, glosses = ge.entry_data(node, tables)
                cases.append((key, node.get('section'), defs, links[key]))
    if args.limit:
        cases = cases[:args.limit]
    print(f'{len(cases)} entries to check ({len(done)} done before)', file=sys.stderr)
    new_file = not os.path.exists(out_path)
    started, finished, kept, other = time.time(), 0, 0, 0
    with open(out_path, 'a', encoding='utf-8') as out, open(review_path, 'a', encoding='utf-8') as review, \
            open(done_path, 'a', encoding='utf-8') as done_file, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        if new_file:
            out.write('wiki\ttitle\tlanguage\tpath\tgloss\tsense_id\tscore\tmargin\taccepted\n')
        futures = {pool.submit(verify, args.verify, key[0], pos, defs, [r['gloss'] for r in rows],
                               [r['sense_id'] for r in rows]): (key, defs, rows) for key, pos, defs, rows in cases}
        try:
            for future in concurrent.futures.as_completed(futures):
                key, defs, rows = futures[future]
                try:
                    v = future.result()
                except OutOfCredit:
                    raise
                except Exception as error:
                    print(f'\n  error {key[0]}: {error}', file=sys.stderr)
                    continue
                texts = dict(defs)
                for k, r in enumerate(rows):
                    second = v.get(k)
                    same = second == r['sense_id']
                    kept += same
                    out.write('\t'.join([r['wiki'], r['title'], r['language'], r['path'], r['gloss'], r['sense_id'],
                                         r['score'], '', '1' if same else '0']) + '\n')
                    if not same:
                        other += 1
                        review.write(f"{r['title']}  «{r['gloss']}»  first {r['sense_id']}: "
                                     f"{texts.get(r['sense_id'], '')[:100]}  |  check {second or 'none'}: "
                                     f"{texts.get(second, '')[:100]}\n")
                done_file.write('\t'.join(key) + '\n')
                finished += 1
                if finished % 10 == 0 or finished == len(cases):
                    out.flush(), review.flush(), done_file.flush()
                    rate = finished / (time.time() - started)
                    left = (len(cases) - finished) / rate if rate else 0
                    print(f'\r  {finished}/{len(cases)} entries, {rate:.2f}/s, {int(left // 60)}:{int(left % 60):02d} left, '
                          f'confirmed {kept}, other {other}, tokens in {TOKENS["in"]} out {TOKENS["out"]}   ', end='',
                          file=sys.stderr, flush=True)
        except (OutOfCredit, KeyboardInterrupt) as e:
            print(f'\n  stopped: {str(e) or "interrupted"}', file=sys.stderr)
            pool.shutdown(wait=False, cancel_futures=True)
    print(f'\n{finished} entries checked: {kept} links confirmed, {other} not (to review: {review_path}); '
          f'for wiktglos --extra {out_path}', file=sys.stderr)


def read_review(path):
    """A review file of --apply (<output>.txt): (title, language, path, gloss) -> (id or None,
    confidence, definition text)"""
    out, head = {}, None
    for line in open(path, encoding='utf-8'):
        if m := re.match(r'^== (.*) \((.*?)\) \[(\S+) (\S*)\]', line):
            head = (m[1], m[3], m[4])
        elif (m := re.match(r'^  «(.*)» -> (\S+) \(([\d.]+)\)(?:  (.*))?$', line.rstrip('\n'))) and head:
            out[head + (m[1],)] = (None if m[2] == 'none' else m[2], float(m[3]), m[4] or '')
    return out


def run_agree(args):
    """Two --apply runs over the same glosses by two models (--agree A.tsv B.tsv, their review
    files A.txt, B.txt beside them): a link accepted where both give the same definition (and
    both with --confidence, unless --any-confidence), -o <A>.agreed.tsv; for review by hand
    <A>.review.tsv: different definitions, and one model with none (both none: no link), with
    an empty column choice (a, b, a definition id, none) for --from-review"""
    first, second = args.agree
    a, b = (read_review(path.removesuffix('.tsv') + '.txt') for path in (first, second))
    wiki = os.path.basename(first).removeprefix('glossllm-').split('.')[0].split('-')[0]
    base = first.removesuffix('.tsv')
    out_path = args.output if args.output != f'glossllm-{args.wiki}.tsv' else base + '.agreed.tsv'
    review_path = base + '.review.tsv'
    count = __import__('collections').Counter()
    with open(out_path, 'w', encoding='utf-8') as out, open(review_path, 'w', encoding='utf-8') as review:
        out.write('wiki\ttitle\tlanguage\tpath\tgloss\tsense_id\tscore\tmargin\taccepted\n')
        review.write('wiki\ttitle\tlanguage\tpath\tgloss\tcase\ta\ta_definition\tb\tb_definition\tchoice\n')
        for key in sorted(a.keys() & b.keys()):
            (x, cx, tx), (y, cy, ty) = a[key], b[key]
            if x is None and y is None:
                count['both none'] += 1
                continue
            if x is not None and x == y:
                sure = args.any_confidence or (cx >= args.confidence and cy >= args.confidence)
                count['agreed' if sure else 'same, unsure'] += 1
                out.write('\t'.join([wiki, *key, x, f'{min(cx, cy):.2f}', '', '1' if sure else '0']) + '\n')
                continue
            case = 'differ' if x and y else 'a none' if x is None else 'b none'
            count[case] += 1
            clean = lambda t: t.replace('\t', ' ')
            review.write('\t'.join([wiki, *key, case, x or 'none', clean(tx), y or 'none', clean(ty), '']) + '\n')
    print(f"{count['agreed']} links agreed{' (any confidence)' if args.any_confidence else ''}, "
          f"{count['same, unsure']} the same but unsure, {count['differ']} different, {count['a none']} none of "
          f"{first} only, {count['b none']} none of {second} only, {count['both none']} none of both; "
          f'for wiktglos --extra {out_path}; by hand: {review_path}', file=sys.stderr)


def run_vote(args):
    """A third model (--model, which may answer null) votes on the review files of --agree:
    choice = the answer it shares with a or b (a none and its null: none), else empty; the
    files rewritten as <review>.voted.tsv with choice, ready for --from-review; in parts"""
    for path in args.vote:
        rows = list(csv.DictReader(open(path, encoding='utf-8'), delimiter='\t'))
        wiki = rows[0]['wiki'] if rows else ''
        out_path = path.removesuffix('.tsv') + '.voted.tsv'
        done = {}
        if os.path.exists(out_path):  # a run before: its votes kept
            for r in csv.DictReader(open(out_path, encoding='utf-8'), delimiter='\t'):
                done[(r['title'], r['language'], r['path'], r['gloss'])] = r
        todo = {}
        for r in rows:
            key = (r['title'], r['language'], r['path'], r['gloss'])
            if key not in done:
                todo.setdefault(key[:3], []).append(r)
        entries = {}
        if todo:
            for page in ge.pages(args.input, wiki):
                for code, where, node, tables in ge.entries(page):
                    key = (page['title'], code, '/'.join(f'{k}:{i}' for k, i in where))
                    if key in todo:
                        entries[key] = (node.get('section'), ge.entry_data(node, tables)[0], examples(node))
        print(f'{path}: {sum(len(v) for v in todo.values())} glosses of {len(todo)} entries to vote '
              f'({len(done)} voted before)', file=sys.stderr)
        started, finished = time.time(), 0
        votes = __import__('collections').Counter()
        fields = (list(rows[0].keys()) if rows else []) + ['c', 'c_definition']
        new_file = not os.path.exists(out_path)
        three_path = path.removesuffix('.tsv') + '.voted.three.txt'
        with open(out_path, 'a', encoding='utf-8') as out, open(three_path, 'a', encoding='utf-8') as three, \
                concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            if new_file:
                out.write('\t'.join(fields) + '\n')
            futures = {pool.submit(ask, args.model, key[0], entries[key][0], entries[key][1],
                                   [r['gloss'] for r in group], fallback=True, notes=entries[key][2]): group
                       for key, group in todo.items() if key in entries}
            try:
                for future in concurrent.futures.as_completed(futures):
                    group = futures[future]
                    try:
                        answer = future.result()
                    except OutOfCredit:
                        raise
                    except Exception as error:
                        print(f"\n  error {group[0]['title']}: {error}", file=sys.stderr)
                        continue
                    texts = dict(entries[(group[0]['title'], group[0]['language'], group[0]['path'])][1])
                    for k, r in enumerate(group):
                        third = answer.get(k, (None, 0.0))[0] or 'none'
                        # Two votes of three win (none is a vote too); three different: choice
                        # empty, c tells the third, for a decision later (<review>.voted.three.txt)
                        choice = 'a' if third == r['a'] else 'b' if third == r['b'] else ''
                        votes[choice or 'third other'] += 1
                        r = dict(r, choice=choice, c=third, c_definition=texts.get(third, ''))
                        out.write('\t'.join((r.get(f) or '').replace('\t', ' ') for f in fields) + '\n')
                        if not choice:
                            three.write(f"{r['title']}  «{r['gloss']}»\n  a {r['a']}: {r['a_definition'][:120]}\n"
                                        f"  b {r['b']}: {r['b_definition'][:120]}\n  c {third}: {texts.get(third, '')[:120]}\n")
                    finished += 1
                    if finished % 10 == 0 or finished == len(futures):
                        out.flush(), three.flush()
                        rate = finished / (time.time() - started)
                        left = (len(futures) - finished) / rate if rate else 0
                        print(f'\r  {finished}/{len(futures)} entries, {rate:.2f}/s, {int(left // 60)}:{int(left % 60):02d} left, '
                              f"a {votes['a']} b {votes['b']} other {votes['third other']}, "
                              f'tokens in {TOKENS["in"]} out {TOKENS["out"]}   ', end='', file=sys.stderr, flush=True)
            except (OutOfCredit, KeyboardInterrupt) as e:
                print(f'\n  stopped: {str(e) or "interrupted"}', file=sys.stderr)
                pool.shutdown(wait=False, cancel_futures=True)
        print(f"\n{path}: votes a {votes['a']}, b {votes['b']}, three different {votes['third other']} "
              f'({three_path}); next: --from-review {out_path}', file=sys.stderr)


def run_from_review(args):
    """Review files with choice filled (a, b, a definition id, none): -o <review>.chosen.tsv for
    wiktglos --extra (method manual)"""
    for path in args.from_review:
        out_path = path.removesuffix('.tsv') + '.chosen.tsv'
        chosen = 0
        with open(out_path, 'w', encoding='utf-8') as out:
            out.write('wiki\ttitle\tlanguage\tpath\tgloss\tsense_id\tscore\tmargin\taccepted\n')
            for r in csv.DictReader(open(path, encoding='utf-8'), delimiter='\t'):
                choice = (r.get('choice') or '').strip()
                sense = r['a'] if choice == 'a' else r['b'] if choice == 'b' else choice
                if not sense or sense == 'none':
                    continue
                out.write('\t'.join([r['wiki'], r['title'], r['language'], r['path'], r['gloss'], sense, '1.00', '',
                                     '1']) + '\n')
                chosen += 1
        print(f'{path}: {chosen} links chosen, for wiktglos --extra {out_path}', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--eval', action='store_true')
    mode.add_argument('--apply', action='store_true')
    mode.add_argument('--check', metavar='TSV', help='the links of a glossllm TSV confirmed by --verify')
    mode.add_argument('--agree', nargs=2, metavar=('A.tsv', 'B.tsv'),
                      help='the links two models agree on (the same glosses by --apply -o of each)')
    mode.add_argument('--vote', nargs='+', metavar='REVIEW.tsv',
                      help='a third model (--model) votes on review files of --agree: <review>.voted.tsv')
    mode.add_argument('--from-review', nargs='+', metavar='REVIEW.tsv',
                      help='review files of --agree with choice filled, to <review>.chosen.tsv')
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
    parser.add_argument('--verify', help='a second model to confirm each answer (cometapi:, zai:): only confirmed kept')
    parser.add_argument('--any-confidence', action='store_true',
                        help='--agree: the same definition of both models whatever their confidence')
    parser.add_argument('--batch', type=int, default=1, help='entries in a request of an API provider; z.ai limits requests at a time, not per minute: 8 to a request were slower (187 s against 126 s on gold-en-hard)')
    parser.add_argument('--limit', type=int, help='the first entries only (--apply: of those not done yet)')
    parser.add_argument('--recheck', action='store_true',
                        help='--apply: also the glosses linked by best and embedding (some 10%% of them wrong)')
    parser.add_argument('--free-only', action='store_true',
                        help='--apply: only the definitions without glosses of the easy methods')
    parser.add_argument('--no-examples', dest='examples', action='store_false',
                        help='--apply: no usage example after each definition')
    args = parser.parse_args()
    args.output = args.output or f'glossllm-{args.wiki}.tsv'
    load_env()
    for model in filter(None, (args.model, args.fallback, args.verify)):  # a missing key: now, not per request
        prefix = model.split(':', 1)[0]
        key = PROVIDERS[prefix][1] if prefix in PROVIDERS else 'ANTHROPIC_API_KEY' if prefix == 'anthropic' else None
        if key and not os.environ.get(key):
            sys.exit(f'{model}: {key} is not set')
    if args.check and not args.verify:
        sys.exit('--check needs --verify MODEL')
    (run_vote if args.vote else run_from_review if args.from_review else run_agree if args.agree else run_check if args.check
     else run_eval if args.eval else run_apply)(args)


if __name__ == '__main__':
    main()
