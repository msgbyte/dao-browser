#!/usr/bin/env python3
"""Translate Dao iOS .strings tables from the canonical English catalog."""

from __future__ import annotations

import argparse
from collections import Counter
import concurrent.futures
import json
import os
from pathlib import Path
import re
import sys
import tempfile

try:
    from scripts import i18n_android as android
except ModuleNotFoundError:  # Run directly as scripts/i18n_ios.py.
    import i18n_android as android


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_RESOURCES_ROOT = ROOT / "ios" / "DaoBrowser" / "Resources"
TABLES = ("Localizable.strings", "InfoPlist.strings")
# Chromium locale codes whose Apple .lproj name differs.
_LPROJ_NAMES = {"zh-CN": "zh-Hans", "zh-TW": "zh-Hant", "iw": "he", "no": "nb"}

_QUOTED = r'"((?:[^"\\]|\\.)*)"'
_TOKEN_RE = re.compile(rf"/\*.*?\*/|//[^\n]*|{_QUOTED}\s*=\s*{_QUOTED}\s*;", re.S)
_ESCAPE_RE = re.compile(r"\\(.)", re.S)
_UNESCAPES = {"n": "\n", "t": "\t", "r": "\r"}
_IOS_PLACEHOLDER_RE = re.compile(
    r"%(?:\d+\$)?[-#+ 0']*\d*(?:\.\d+)?(?:hh|h|ll|l|q|z|t|j)?[@dDuUxXoOfFeEgGcCsSpaA%]"
)


def lproj_name(locale_code: str) -> str:
    normalized = locale_code.replace("_", "-")
    return _LPROJ_NAMES.get(normalized, normalized)


def _unescape(value: str) -> str:
    return _ESCAPE_RE.sub(lambda match: _UNESCAPES.get(match[1], match[1]), value)


def _escape(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\t", "\\t")
        .replace("\r", "\\r")
    )


def parse_strings_text(text: str) -> dict[str, str]:
    if _TOKEN_RE.sub("", text).strip():
        raise ValueError("Unrecognized content in .strings table")
    entries: dict[str, str] = {}
    for match in _TOKEN_RE.finditer(text):
        if match[1] is not None:
            entries[_unescape(match[1])] = _unescape(match[2])
    return entries


def parse_strings(path: Path) -> dict[str, str]:
    return parse_strings_text(path.read_text(encoding="utf-8"))


def extract_ios_placeholders(value: str) -> tuple[str, ...]:
    return tuple(_IOS_PLACEHOLDER_RE.findall(value))


def validate_translation(source: dict[str, str], translated: dict[str, str]) -> None:
    if set(source) != set(translated):
        missing = sorted(set(source) - set(translated))
        extra = sorted(set(translated) - set(source))
        raise ValueError(f"Translation keys differ: missing={missing}, extra={extra}")
    for key, source_value in source.items():
        if Counter(extract_ios_placeholders(source_value)) != Counter(
            extract_ios_placeholders(translated[key])
        ):
            raise ValueError(f"iOS placeholders changed for {key}")


def write_strings(destination: Path, source: dict[str, str], translations: dict[str, str]) -> None:
    validate_translation(source, translations)
    text = "".join(f'"{_escape(key)}" = "{_escape(translations[key])}";\n' for key in source)
    if parse_strings_text(text) != {key: translations[key] for key in source}:
        raise ValueError("Generated .strings table does not round-trip")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=destination.parent, suffix=".tmp", delete=False
    ) as temporary:
        temporary.write(text)
    os.replace(temporary.name, destination)


def _translation_prompt(locale_code: str, strings: dict[str, str]) -> str:
    payload = json.dumps(strings, ensure_ascii=False, indent=2)
    return f"""You are a senior iOS localization engineer.

Translate the following Dao Browser iOS UI strings from English to {locale_code}.

Strict rules:
- Return only one JSON object with exactly the same keys.
- Preserve every iOS format placeholder exactly, including %@, %d, %1$@, %lld, and %%.
- Preserve product names, URLs, file names, whitespace, and meaningful punctuation.
- Use concise, natural wording suitable for a mobile browser interface.
- Do not add explanations or Markdown fences.

Source strings:
{payload}
"""


def translate_batch(source: dict[str, str], locale_code: str, api_key: str, model: str) -> dict[str, str]:
    raw = android.call_openai(_translation_prompt(locale_code, source), api_key, model)
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as error:
        raise ValueError(f"OpenAI returned invalid JSON for {locale_code}") from error
    if not isinstance(parsed, dict):
        raise ValueError(f"OpenAI returned a non-object for {locale_code}")
    translated = {str(key): str(value) for key, value in parsed.items()}
    validate_translation(source, translated)
    return translated


def process_locale(
    locale_code: str,
    resources_root: Path,
    force: bool,
    dry_run: bool,
    api_key: str | None,
    model: str,
) -> list[str]:
    lines: list[str] = []
    for table in TABLES:
        source = parse_strings(resources_root / "en.lproj" / table)
        destination = resources_root / f"{lproj_name(locale_code)}.lproj" / table
        existing = parse_strings(destination) if destination.exists() else {}
        label = f"[{locale_code}/ios] {destination.parent.name}/{table}"

        if not force and set(existing) == set(source):
            validate_translation(source, existing)
            lines.append(f"{label} up-to-date")
            continue

        pending = source if force else {
            key: value for key, value in source.items() if key not in existing
        }
        if dry_run:
            lines.append(f"{label} would translate {len(pending)} strings")
            continue
        if api_key is None:
            raise ValueError("OPENAI_API_KEY or OPENAPI_KEY is required outside dry-run")

        translated = {} if force else {key: existing[key] for key in source if key in existing}
        for batch in android._chunks(list(pending.items())):
            translated.update(translate_batch(batch, locale_code, api_key, model))
        write_strings(destination, source, translated)
        lines.append(f"{label} wrote {len(translated)} entries")
    return lines


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Translate Dao iOS resources from English via OpenAI.")
    parser.add_argument("--langs", help="Comma-separated locale list. Default: all configured locales.")
    parser.add_argument("--force", action="store_true", help="Overwrite complete locale tables.")
    parser.add_argument("--model", default=android.OPENAI_MODEL, help="OpenAI model ID. Default: gpt-5.5.")
    parser.add_argument("--dry-run", action="store_true", help="Plan without API calls or file writes.")
    parser.add_argument("--jobs", type=int, default=4, help="Locales translated in parallel. Default: 4.")
    parser.add_argument("--resources-root", type=Path, default=DEFAULT_RESOURCES_ROOT, help=argparse.SUPPRESS)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    locales = (
        [code.strip() for code in args.langs.split(",") if code.strip()]
        if args.langs
        else android.configured_locales()
    )
    if not locales:
        print("error: no iOS translation locales are configured", file=sys.stderr)
        return 2

    api_key = None
    if not args.dry_run:
        api_key = os.environ.get("OPENAI_API_KEY") or os.environ.get("OPENAPI_KEY")
        if not api_key:
            print("error: OPENAI_API_KEY or OPENAPI_KEY is required (or pass --dry-run).", file=sys.stderr)
            return 2

    failed = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {
            pool.submit(
                process_locale, code, args.resources_root, args.force, args.dry_run, api_key, args.model
            ): code
            for code in locales
        }
        for future in concurrent.futures.as_completed(futures):
            try:
                print("\n".join(future.result()))
            except Exception as error:
                failed = True
                print(f"[{futures[future]}/ios] FAILED: {error}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
