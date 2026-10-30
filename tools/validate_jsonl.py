#!/usr/bin/env python3
"""Validates wikt2jsonl output against res/schema/page.schema.json, or with --uniform the output of
wikt2jsonl --uniform against res/schema/uniform.schema.json.

validate_jsonl.py [--uniform] [jsonl_dir] (default jsonl): prints errors and a count per file.
"""
import json
import pathlib
import sys

import jsonschema

args = [a for a in sys.argv[1:] if a != "--uniform"]
schema_name = "uniform.schema.json" if "--uniform" in sys.argv[1:] else "page.schema.json"
schema_file = pathlib.Path(__file__).resolve().parent.parent / "res" / "schema" / schema_name
validator = jsonschema.Draft202012Validator(json.loads(schema_file.read_text()))
directory = pathlib.Path(args[0] if args else "jsonl")
failed = pages = 0
for file in sorted(directory.glob("*.jsonl")):
    lines = errors = 0
    for number, line in enumerate(file.open(encoding="utf-8"), 1):
        lines += 1
        for error in validator.iter_errors(json.loads(line)):
            errors += 1
            if errors <= 3:
                print(f"{file.name}:{number}: {'/'.join(map(str, error.absolute_path))}: {error.message[:200]}")
    pages += lines
    if errors:
        failed += 1
        print(f"{file.name}: {errors} errors in {lines} lines")
print(f"{pages} pages, {failed} files with errors")
sys.exit(1 if failed else 0)
