#!/usr/bin/env python3
"""Translate human-readable changelogs, preserving platform/version structure."""

from __future__ import annotations

import argparse
from collections import Counter
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

try:
    from scripts import i18n_android as android
except ModuleNotFoundError:
    import i18n_android as android


ROOT = Path(__file__).resolve().parent.parent / 'docs' / 'changelog'
TEXT = re.compile(r'^(# |## \[(?:desktop|android|ios)\] |### \[Unreleased\] |- )(.+)$')
CACHED = re.compile(r'^(.*?) <!-- source:([a-f0-9]{64}) -->$', re.M)
TOKENS = re.compile(r'`[^`]+`|\]\([^)]*\)|https?://[^\s)]+')


def process_locale(locale: str, root: Path, force: bool, dry_run: bool,
                   api_key: str | None, model: str) -> str:
    if locale in ('en', 'zh-CN'):
        return f'[{locale}/changelog] protected hand-authored source'
    if not re.fullmatch(r'[a-z]{2,3}(?:-[A-Za-z0-9]+)*', locale):
        raise ValueError(f'Invalid locale: {locale}')
    lines = (root / 'en.md').read_text(encoding='utf-8').splitlines()
    destination = root / f'{locale}.md'
    previous = destination.read_text(encoding='utf-8') if destination.exists() else ''
    cached = {} if force else {key: text for text, key in CACHED.findall(previous)}
    source = {}
    for line in lines:
        match = TEXT.fullmatch(line)
        if match:
            key = hashlib.sha256(line.encode()).hexdigest()
            source[key] = match[2]
    pending = {key: value for key, value in source.items() if key not in cached}
    if dry_run:
        return f'[{locale}/changelog] would translate {len(pending)} strings'
    translated = {}
    for batch in android._chunks(list(pending.items())):
        if not api_key:
            raise ValueError('OPENAI_API_KEY or OPENAPI_KEY is required.')
        prompt = (
            f'Translate Dao Browser changelog text from English to {locale}. '
            'Use concise, natural release-note prose. Return only a JSON object with '
            'exactly the same keys and string values. Preserve Markdown, inline code, '
            'URLs, versions and product names. Each value must be one nonempty line.\n'
            'Source strings:\n' + json.dumps(batch, ensure_ascii=False)
        )
        result = json.loads(android.call_openai(prompt, api_key, model))
        if not isinstance(result, dict) or set(result) != set(batch):
            raise ValueError(f'{locale}: translation keys differ from source')
        for key, value in result.items():
            if (not isinstance(value, str) or not value.strip() or '\n' in value
                    or '\r' in value or '<!--' in value
                    or Counter(TOKENS.findall(value)) != Counter(TOKENS.findall(batch[key]))):
                raise ValueError(f'{locale}: invalid Markdown translation for {key}')
        translated.update(result)
    output = []
    for line in lines:
        match = TEXT.fullmatch(line)
        if match:
            key = hashlib.sha256(line.encode()).hexdigest()
            line = cached.get(key) if key in cached else match[1] + translated[key]
            line += f' <!-- source:{key} -->'
        output.append(line)
    text = '\n'.join(output) + '\n'
    if text != previous:
        temporary = None
        try:
            with tempfile.NamedTemporaryFile('w', encoding='utf-8', dir=root, delete=False) as file:
                temporary = Path(file.name)
                file.write(text)
            os.replace(temporary, destination)
        finally:
            if temporary:
                temporary.unlink(missing_ok=True)
    return f'[{locale}/changelog] translated {len(pending)} strings'


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--langs', help='Comma-separated locales; default: configured desktop locales.')
    parser.add_argument('--force', action='store_true', help='Retranslate generated locales; never overwrite en/zh-CN.')
    parser.add_argument('--model', default=android.OPENAI_MODEL)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args(argv)
    locales = args.langs.split(',') if args.langs else android.configured_locales()
    api_key = os.environ.get('OPENAI_API_KEY') or os.environ.get('OPENAPI_KEY')
    failed = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(process_locale, locale.strip(), ROOT, args.force,
                               args.dry_run, api_key, args.model) for locale in set(locales)]
        for future in concurrent.futures.as_completed(futures):
            try:
                print(future.result())
            except Exception as error:
                failed = True
                print(f'changelog translation failed: {error}', file=sys.stderr)
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
