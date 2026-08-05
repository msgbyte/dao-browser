#!/usr/bin/env python3
"""Translate Dao Android string resources from the canonical English catalog."""

from __future__ import annotations

import argparse
from collections import Counter
import concurrent.futures
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import time
import urllib.error
import urllib.request
from xml.etree import ElementTree as ET


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE = ROOT / "android" / "app" / "src" / "main" / "res" / "values" / "strings.xml"
DEFAULT_RESOURCES_ROOT = DEFAULT_SOURCE.parent.parent
DESKTOP_TRANSLATIONS = ROOT / "src" / "dao" / "browser" / "strings" / "translations"
OPENAI_BASE_URL = os.environ.get("OPENAI_BASE_URL", "https://api.openai.com/v1").rstrip("/")
OPENAI_MODEL = os.environ.get("OPENAI_TRANSLATE_MODEL", "gpt-5.5")
TRANSLATION_BATCH_SIZE = 60
MAX_API_ATTEMPTS = 3


@dataclass(frozen=True)
class AndroidString:
    name: str
    value: str
    translatable: bool = True


_ANDROID_PLACEHOLDER_RE = re.compile(
    r"%(?:\d+\$)?[-#+ 0,(<]*\d*(?:\.\d+)?(?:[tT][A-Za-z]|[A-Za-z%])"
)


def _inner_xml(element: ET.Element) -> str:
    parts = [element.text or ""]
    for child in element:
        parts.append(ET.tostring(child, encoding="unicode", short_empty_elements=True))
    return "".join(parts).replace("\\'", "'").replace("\\n", "\n").replace("\\t", "\t")


def parse_android_strings(path: Path) -> list[AndroidString]:
    root = ET.parse(path).getroot()
    entries: list[AndroidString] = []
    for element in root.findall("string"):
        name = element.get("name")
        if not name:
            raise ValueError(f"Android string without a name in {path}")
        entries.append(
            AndroidString(
                name=name,
                value=_inner_xml(element),
                translatable=element.get("translatable", "true") != "false",
            )
        )
    return entries


def extract_android_placeholders(value: str) -> tuple[str, ...]:
    return tuple(_ANDROID_PLACEHOLDER_RE.findall(value))


def android_resource_qualifier(locale_code: str) -> str:
    normalized = locale_code.replace("_", "-")
    parts = normalized.split("-")
    if len(parts) == 1:
        return parts[0].lower()

    language = parts[0].lower()
    if len(parts) == 2 and len(parts[1]) == 2 and parts[1].isalpha():
        return f"{language}-r{parts[1].upper()}"

    bcp47_parts = [language]
    for part in parts[1:]:
        if len(part) == 4 and part.isalpha():
            bcp47_parts.append(part.title())
        elif len(part) == 2 and part.isalpha():
            bcp47_parts.append(part.upper())
        else:
            bcp47_parts.append(part)
    return "b+" + "+".join(bcp47_parts)


_MARKUP_TAG_RE = re.compile(r"</?([A-Za-z][A-Za-z0-9:_-]*)(?:\s[^>]*)?/?>")
_BARE_AMPERSAND_RE = re.compile(r"&(?!#\d+;|#x[0-9A-Fa-f]+;|[A-Za-z][A-Za-z0-9]+;)")


def _markup_tags(value: str) -> tuple[str, ...]:
    return tuple(match.group(0) for match in _MARKUP_TAG_RE.finditer(value))


def validate_translation(source: dict[str, str], translated: dict[str, str]) -> None:
    source_keys = set(source)
    translated_keys = set(translated)
    if source_keys != translated_keys:
        missing = sorted(source_keys - translated_keys)
        extra = sorted(translated_keys - source_keys)
        raise ValueError(f"Translation keys differ: missing={missing}, extra={extra}")

    for name, source_value in source.items():
        translated_value = translated[name]
        source_placeholders = Counter(extract_android_placeholders(source_value))
        translated_placeholders = Counter(extract_android_placeholders(translated_value))
        if source_placeholders != translated_placeholders:
            raise ValueError(
                f"Android placeholders changed for {name}: "
                f"{tuple(source_placeholders.elements())} != "
                f"{tuple(translated_placeholders.elements())}"
            )
        if _markup_tags(source_value) != _markup_tags(translated_value):
            raise ValueError(f"Inline markup changed for {name}")


def _escape_android_text(value: str | None) -> str | None:
    if value is None:
        return None
    return value.replace("'", "\\'").replace("\n", "\\n").replace("\t", "\\t")


def _set_inner_xml(element: ET.Element, value: str) -> None:
    safe_fragment = _BARE_AMPERSAND_RE.sub("&amp;", value)
    try:
        wrapper = ET.fromstring(f"<wrapper>{safe_fragment}</wrapper>")
    except ET.ParseError as error:
        raise ValueError(f"Invalid inline markup in Android string: {value!r}") from error

    element.text = _escape_android_text(wrapper.text)
    for child in list(wrapper):
        wrapper.remove(child)
        for node in child.iter():
            node.text = _escape_android_text(node.text)
            node.tail = _escape_android_text(node.tail)
        element.append(child)


def write_android_catalog(
    destination: Path,
    source_entries: list[AndroidString],
    translations: dict[str, str],
) -> None:
    translatable_source = {
        entry.name: entry.value for entry in source_entries if entry.translatable
    }
    validate_translation(translatable_source, translations)

    resources = ET.Element("resources")
    for entry in source_entries:
        attributes = {"name": entry.name}
        if not entry.translatable:
            attributes["translatable"] = "false"
        element = ET.SubElement(resources, "string", attributes)
        _set_inner_xml(
            element,
            translations[entry.name] if entry.translatable else entry.value,
        )

    resources.text = "\n    "
    for index, element in enumerate(resources):
        element.tail = "\n" if index == len(resources) - 1 else "\n    "
    tree = ET.ElementTree(resources)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=f".{destination.name}.",
            suffix=".tmp",
            dir=destination.parent,
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
        tree.write(temporary_path, encoding="utf-8", xml_declaration=True)

        written_entries = parse_android_strings(temporary_path)
        expected_values = [
            translations[entry.name] if entry.translatable else entry.value
            for entry in source_entries
        ]
        if [entry.name for entry in written_entries] != [entry.name for entry in source_entries]:
            raise ValueError("Generated Android catalog changed string order")
        if [entry.value for entry in written_entries] != expected_values:
            raise ValueError("Generated Android catalog changed string values")
        os.replace(temporary_path, destination)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def configured_locales() -> list[str]:
    return sorted(
        path.stem.removeprefix("dao_strings_")
        for path in DESKTOP_TRANSLATIONS.glob("dao_strings_*.xtb")
    )


def _translation_prompt(locale_code: str, strings: dict[str, str]) -> str:
    payload = json.dumps(strings, ensure_ascii=False, indent=2)
    return f"""You are a senior Android localization engineer.

Translate the following Dao Browser Android UI strings from English to {locale_code}.

Strict rules:
- Return only one JSON object with exactly the same keys.
- Preserve every Android placeholder exactly, including %1$s, %2$d, %s, and %%.
- Preserve inline XML tags exactly and translate only their text content.
- Preserve product names, URLs, file names, whitespace, and meaningful punctuation.
- Use concise, natural wording suitable for a mobile browser interface.
- Do not add explanations or Markdown fences.

Source strings:
{payload}
"""


def call_openai(prompt: str, api_key: str, model: str) -> str:
    request = urllib.request.Request(
        f"{OPENAI_BASE_URL}/chat/completions",
        data=json.dumps(
            {
                "model": model,
                "response_format": {"type": "json_object"},
                "messages": [{"role": "user", "content": prompt}],
            }
        ).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
    )
    for attempt in range(1, MAX_API_ATTEMPTS + 1):
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                body = json.loads(response.read())
            return body["choices"][0]["message"]["content"]
        except urllib.error.HTTPError as error:
            if error.code != 429 and error.code < 500:
                raise
            if attempt == MAX_API_ATTEMPTS:
                raise
        except urllib.error.URLError:
            if attempt == MAX_API_ATTEMPTS:
                raise
        time.sleep(2 ** (attempt - 1))
    raise RuntimeError("OpenAI request exhausted all retry attempts")


def translate_batch(
    source: dict[str, str],
    locale_code: str,
    api_key: str,
    model: str,
) -> dict[str, str]:
    raw = call_openai(_translation_prompt(locale_code, source), api_key, model)
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as error:
        raise ValueError(f"OpenAI returned invalid JSON for {locale_code}") from error
    if not isinstance(parsed, dict):
        raise ValueError(f"OpenAI returned a non-object for {locale_code}")
    translated = {str(key): str(value) for key, value in parsed.items()}
    validate_translation(source, translated)
    return translated


def _chunks(entries: list[tuple[str, str]]) -> list[dict[str, str]]:
    return [
        dict(entries[index:index + TRANSLATION_BATCH_SIZE])
        for index in range(0, len(entries), TRANSLATION_BATCH_SIZE)
    ]


def process_locale(
    locale_code: str,
    source_entries: list[AndroidString],
    resources_root: Path,
    force: bool,
    dry_run: bool,
    api_key: str | None,
    model: str,
) -> list[str]:
    destination = resources_root / f"values-{android_resource_qualifier(locale_code)}" / "strings.xml"
    existing_entries = parse_android_strings(destination) if destination.exists() else []
    existing = {
        entry.name: entry.value for entry in existing_entries if entry.translatable
    }
    source = {
        entry.name: entry.value for entry in source_entries if entry.translatable
    }

    if not force and set(existing) == set(source):
        validate_translation(source, existing)
        return [f"[{locale_code}/android] up-to-date"]

    pending = source if force else {
        name: value for name, value in source.items() if name not in existing
    }
    if dry_run:
        return [f"[{locale_code}/android] would translate {len(pending)} strings"]
    if api_key is None:
        raise ValueError("OPENAI_API_KEY is required outside dry-run")

    translated = {} if force else dict(existing)
    for batch in _chunks(list(pending.items())):
        translated.update(translate_batch(batch, locale_code, api_key, model))
    validate_translation(source, translated)
    write_android_catalog(destination, source_entries, translated)
    return [
        f"[{locale_code}/android] wrote {destination.parent.name}/strings.xml "
        f"({len(translated)} entries)"
    ]


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Translate Dao Android resources from English via OpenAI."
    )
    parser.add_argument("--langs", help="Comma-separated locale list. Default: all configured locales.")
    parser.add_argument("--force", action="store_true", help="Overwrite complete locale catalogs.")
    parser.add_argument("--model", default=OPENAI_MODEL, help="OpenAI model ID. Default: gpt-5.5.")
    parser.add_argument("--dry-run", action="store_true", help="Plan without API calls or file writes.")
    parser.add_argument("--jobs", type=int, default=4, help="Locales translated in parallel. Default: 4.")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE, help=argparse.SUPPRESS)
    parser.add_argument("--resources-root", type=Path, default=DEFAULT_RESOURCES_ROOT, help=argparse.SUPPRESS)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    source_entries = parse_android_strings(args.source)
    locales = (
        [code.strip() for code in args.langs.split(",") if code.strip()]
        if args.langs
        else configured_locales()
    )
    if not locales:
        print("error: no Android translation locales are configured", file=sys.stderr)
        return 2

    if args.dry_run:
        api_key = None
    else:
        api_key = os.environ.get("OPENAI_API_KEY")
        if not api_key:
            print("error: OPENAI_API_KEY is required (or pass --dry-run).", file=sys.stderr)
            return 2

    jobs = max(1, args.jobs)
    failures: list[tuple[str, Exception]] = []
    if jobs == 1:
        for locale_code in locales:
            try:
                for line in process_locale(
                    locale_code,
                    source_entries,
                    args.resources_root,
                    args.force,
                    args.dry_run,
                    api_key,
                    args.model,
                ):
                    print(line)
            except Exception as error:
                failures.append((locale_code, error))
                print(f"[{locale_code}/android] FAILED: {error}", file=sys.stderr)
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {
                pool.submit(
                    process_locale,
                    locale_code,
                    source_entries,
                    args.resources_root,
                    args.force,
                    args.dry_run,
                    api_key,
                    args.model,
                ): locale_code
                for locale_code in locales
            }
            for future in concurrent.futures.as_completed(futures):
                locale_code = futures[future]
                try:
                    for line in future.result():
                        print(line)
                except Exception as error:
                    failures.append((locale_code, error))
                    print(f"[{locale_code}/android] FAILED: {error}", file=sys.stderr)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
