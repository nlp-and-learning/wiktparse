#!/usr/bin/env python3
"""Validates wikt2jsonl output against res/schema/page.schema.json.

validate_jsonl.py [jsonl_dir] (default jsonl): prints errors and a count per file.
"""
import json
import pathlib
import sys

import jsonschema

schema_file = pathlib.Path(__file__).resolve().parent.parent / "res" / "schema" / "page.schema.json"
validator = jsonschema.Draft202012Validator(json.loads(schema_file.read_text()))
directory = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "jsonl")
failed = 0
for file in sorted(directory.glob("*.jsonl")):
    lines = errors = 0
    for number, line in enumerate(file.open(encoding="utf-8"), 1):
        lines += 1
        for error in validator.iter_errors(json.loads(line)):
            errors += 1
            if errors <= 3:
                print(f"{file.name}:{number}: {'/'.join(map(str, error.absolute_path))}: {error.message[:200]}")
    if errors:
        failed += 1
        print(f"{file.name}: {errors} errors in {lines} lines")
print(f"{failed} files with errors")
sys.exit(1 if failed else 0)
