#!/usr/bin/env python3
"""Add Windows dependencies without replacing the user's gclient settings."""

import ast
import json
from pathlib import Path
import sys


MARKER = b'# Dao: retain Windows dependencies for cross compilation.'


def configure(contents: bytes) -> bytes:
    source = contents.decode('utf-8')
    assignments = [node for node in ast.parse(source).body
                   if isinstance(node, ast.Assign) and any(
                       isinstance(target, ast.Name) and target.id == 'target_os'
                       for target in node.targets)]
    if len(assignments) > 1:
        raise ValueError('Expected one target_os assignment in .gclient.')
    newline = b'\r\n' if b'\r\n' in contents else b'\n'
    if assignments:
        assignment = assignments[0]
        value = assignment.value
        if len(assignment.targets) != 1 or not isinstance(value, ast.List):
            raise ValueError('Set target_os to a literal list in .gclient.')
        targets = ast.literal_eval(value)
        if not all(isinstance(target, str) for target in targets):
            raise ValueError('target_os must contain only platform names.')
        if 'win' not in targets:
            # AST columns count UTF-8 bytes. Retain every byte outside the value.
            lines = contents.splitlines(keepends=True)
            start = sum(map(len, lines[:value.lineno - 1])) + value.col_offset
            end = sum(map(len, lines[:value.end_lineno - 1])) + value.end_col_offset
            replacement = json.dumps(targets + ['win']).encode('utf-8')
            contents = contents[:start] + replacement + contents[end:]
    else:
        contents = contents.rstrip(b'\r\n') + newline + b'target_os = ["win"]' + newline
    if MARKER not in contents.splitlines():
        contents = contents.rstrip(b'\r\n') + newline + MARKER + newline
    return contents


def main():
    filename = Path(sys.argv[1])
    original = filename.read_bytes()
    updated = configure(original)
    if updated != original:
        filename.write_bytes(updated)


if __name__ == '__main__':
    main()
