#!/usr/bin/env python3
"""Lint guard for Lit reactive properties in Dao WebUI TypeScript code.

Chromium's WebUI TypeScript build uses `target: ESNext` + the ES class-field
semantics (`useDefineForClassFields: true`). Under those semantics, a
class-field initializer like

    protected pendingPageAttachment_: PageInfo | null = null;

emits `Object.defineProperty(this, 'pendingPageAttachment_', ...)` at
construction, which REPLACES the reactive accessor Lit installed from
`static get properties() { ... }`. The property becomes a plain data slot
and subsequent assignments NEVER trigger a re-render.

This script scans all Dao WebUI TypeScript files, finds Lit components
(classes with `static override get properties()` or `static get
properties()`), and fails if any reactive property is still declared as a
class-field initializer rather than `declare` + constructor assignment.

Run via `npm run lint:lit` or directly:
    python3 scripts/lint-lit-reactive-fields.py

Exit 0 on clean, 1 on violations (prints file:line and the offending line).
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCAN_DIRS = [
    ROOT / "src" / "dao" / "browser" / "ui" / "webui" / "resources",
]

PROPS_HEADER = re.compile(
    r"static\s+(override\s+)?get\s+properties\s*\(\s*\)\s*\{")
PROP_NAME = re.compile(r"^\s+(\w+)\s*:")
# A class-field initializer: `[modifier]? name [: Type]? = value;`
FIELD_INIT = re.compile(
    r"^(\s*)((?:protected|private|public|readonly|\s)+)?"
    r"(\w+)"
    r"(\s*:\s*[^=;]+?)?"
    r"\s*=\s*"
    r"([^;]+?);\s*$")


def gather_reactive_props(text: str) -> set[str]:
    """Extract the set of property names declared inside
    `static [override] get properties() { return { ... } }` blocks.
    Handles nested object literals via brace-depth counting.
    """
    props: set[str] = set()
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        if PROPS_HEADER.search(lines[i]):
            depth = 0
            started = False
            while i < len(lines):
                depth += lines[i].count("{") - lines[i].count("}")
                pm = PROP_NAME.match(lines[i])
                if pm:
                    props.add(pm.group(1))
                started = started or ("{" in lines[i])
                i += 1
                if started and depth <= 0:
                    break
        else:
            i += 1
    return props


def find_violations(path: pathlib.Path) -> list[tuple[int, str, str]]:
    """Return (line_number, field_name, raw_line) for each reactive prop
    that still has a class-field initializer instead of `declare` + ctor.
    """
    text = path.read_text()
    props = gather_reactive_props(text)
    if not props:
        return []
    hits: list[tuple[int, str, str]] = []
    for idx, raw in enumerate(text.splitlines(), start=1):
        m = FIELD_INIT.match(raw)
        if not m:
            continue
        modifiers = m.group(2) or ""
        name = m.group(3)
        if name not in props:
            continue
        if "declare" in modifiers:
            continue
        hits.append((idx, name, raw))
    return hits


def main() -> int:
    total_violations = 0
    scanned = 0
    for base in SCAN_DIRS:
        if not base.exists():
            continue
        for ts_path in base.rglob("*.ts"):
            # Skip vendor/generated files.
            if "vendor" in ts_path.parts:
                continue
            if "chromium_types" in ts_path.name:
                continue
            scanned += 1
            violations = find_violations(ts_path)
            if not violations:
                continue
            total_violations += len(violations)
            rel = ts_path.relative_to(ROOT)
            print(f"\n{rel}:")
            for line_no, name, raw in violations:
                print(f"  L{line_no}: reactive prop `{name}` uses a "
                      f"class-field initializer — Lit reactivity will NOT "
                      f"work:")
                print(f"      {raw.strip()}")

    if total_violations == 0:
        print(f"lint-lit-reactive-fields: OK ({scanned} files scanned, 0 "
              f"violations)")
        return 0

    print(
        f"\nlint-lit-reactive-fields: FAIL — {total_violations} "
        f"violation(s) found.\n\n"
        "Each flagged field is a reactive Lit property declared in "
        "`static get properties()` but still has a class-field initializer. "
        "With Chromium's `target: ESNext` + `useDefineForClassFields: true` "
        "TS config, class-field initializers emit "
        "`Object.defineProperty(this, ...)` at construction, which replaces "
        "the reactive accessor Lit installed. Property assignments after "
        "that point do NOT trigger re-render.\n\n"
        "Fix pattern:\n"
        "    // BEFORE (broken):\n"
        "    protected pendingPageAttachment_: PageInfo | null = null;\n\n"
        "    // AFTER (fixed):\n"
        "    declare protected pendingPageAttachment_: PageInfo | null;\n\n"
        "    constructor() {\n"
        "      super();\n"
        "      this.pendingPageAttachment_ = null;\n"
        "    }\n",
        file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
