#!/usr/bin/env python3
"""
Auto-translate Dao i18n source strings into all configured locales.

Reads:
  - src/dao/browser/strings/dao_strings.grd (canonical English <message> set)
  - src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts (WebUI source)

Writes:
  - src/dao/browser/strings/translations/dao_strings_<lang>.xtb
  - src/dao/browser/ui/webui/resources/agent/i18n/locales/<lang>.ts

Translation provider: OpenAI Chat Completions API, default model gpt-5.4.
Uses OPENAI_API_KEY (and optional OPENAI_BASE_URL) from environment.

Skips locales that already have translations unless --force is passed.
Always preserves placeholder tokens: ``<ph name="X">$N</ph>`` for grd /
``{name}`` for the WebUI dictionary.

Usage (typically wrapped by scripts/i18n-translate.sh):

  OPENAI_API_KEY=sk-... python3 scripts/i18n-translate.py
  OPENAI_API_KEY=sk-... python3 scripts/i18n-translate.py --langs zh-TW,ja
  OPENAI_API_KEY=sk-... python3 scripts/i18n-translate.py --force --langs zh-TW
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import sys
import time
import urllib.request
import urllib.error
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
GRD = ROOT / "src" / "dao" / "browser" / "strings" / "dao_strings.grd"
XTB_DIR = ROOT / "src" / "dao" / "browser" / "strings" / "translations"
WEBUI_LOCALES = ROOT / "src" / "dao" / "browser" / "ui" / "webui" / \
    "resources" / "agent" / "i18n" / "locales"

# Make the in-tree grit library importable so we can reuse its message-id
# fingerprint function — that is what xtb files key off of.
GRIT_DIR = ROOT / "engine" / "src" / "tools" / "grit"
if GRIT_DIR.exists():
    sys.path.insert(0, str(GRIT_DIR))
try:
    from grit.extern.tclib import GenerateMessageId  # type: ignore
except ImportError:
    print("error: grit library not found. Run `npm run setup` first so "
          f"engine/src/tools/grit exists at {GRIT_DIR}.",
          file=sys.stderr)
    sys.exit(1)


OPENAI_BASE_URL = os.environ.get(
    "OPENAI_BASE_URL", "https://api.openai.com/v1"
).rstrip("/")
OPENAI_MODEL = os.environ.get("OPENAI_TRANSLATE_MODEL", "gpt-5.4")


# ---------- grd parsing ----------

def parse_grd_messages(grd_path: Path) -> List[Tuple[str, str, str]]:
    """Return [(name, source_text_for_fingerprint, presentable_text), ...].

    grit's fingerprint is computed over the *presentable* form of the message
    — i.e. with each <ph name="X">...</ph> replaced by ``X``. Translations
    in xtb keep <ph name="X" /> placeholders.
    """
    tree = ET.parse(grd_path)
    root = tree.getroot()
    out: List[Tuple[str, str, str]] = []
    for msg in root.iter("message"):
        name = msg.get("name") or ""
        if not name:
            continue
        # Build presentable text: text + ph(name) + tail, recursively flat.
        parts: List[str] = []
        if msg.text:
            parts.append(msg.text)
        for child in msg:
            if child.tag == "ph":
                parts.append(child.get("name") or "")
            if child.tail:
                parts.append(child.tail)
        presentable = "".join(parts).strip()
        # Source we feed to the LLM: keep placeholders visible as
        # `<ph name="X">$N</ph>` so the model can reproduce them. The <ex>
        # child of <ph> is a translator hint, not part of the user-visible
        # placeholder — strip it before assembling.
        src_parts: List[str] = []
        if msg.text:
            src_parts.append(msg.text)
        for child in msg:
            if child.tag == "ph":
                ph_name = child.get("name") or ""
                # Inner content of <ph> minus any <ex> subelements.
                inner_parts: List[str] = []
                if child.text:
                    inner_parts.append(child.text)
                for sub in child:
                    if sub.tag == "ex":
                        continue  # translator hint, skip
                    inner_parts.append("".join(sub.itertext()))
                    if sub.tail:
                        inner_parts.append(sub.tail)
                inner = "".join(inner_parts).strip()
                src_parts.append(f'<ph name="{ph_name}">{inner}</ph>')
            if child.tail:
                src_parts.append(child.tail)
        source = "".join(src_parts).strip()
        out.append((name, presentable, source))
    return out


# ---------- xtb parsing & writing ----------

XTB_HEADER = '<?xml version="1.0" ?>\n<!DOCTYPE translationbundle>\n'


def read_existing_xtb(path: Path) -> Dict[str, str]:
    """Return id -> translation text from an existing .xtb."""
    if not path.exists() or path.stat().st_size == 0:
        return {}
    out: Dict[str, str] = {}
    try:
        tree = ET.parse(path)
    except ET.ParseError:
        return {}
    for trans in tree.getroot().iter("translation"):
        tid = trans.get("id")
        if not tid:
            continue
        # Reconstruct the inner content with <ph name="X" /> as-is.
        parts: List[str] = []
        if trans.text:
            parts.append(trans.text)
        for child in trans:
            if child.tag == "ph":
                parts.append(f'<ph name="{child.get("name")}" />')
            if child.tail:
                parts.append(child.tail)
        out[tid] = "".join(parts)
    return out


# Match malformed `<ph>` forms the LLM occasionally emits and collapse them
# to the xtb-standard self-closing form. Two variants observed in the wild:
#   A) <ph name="X">whatever</ph>     (grd-style open/close)
#   B) <ph name="X" />whatever</ph>   (self-close + redundant close)
# The xtb spec only allows `<ph name="X" />` — the inner text is supplied at
# substitution time. Letting either variant through breaks grit's XML parse.
_PH_NORMALIZE = re.compile(r'<ph\s+name="([^"]+)"\s*(?:/)?>[^<]*</ph>')


def normalize_xtb_translation(text: str) -> str:
    return _PH_NORMALIZE.sub(r'<ph name="\1" />', text)


def write_xtb(path: Path, lang: str, translations: Dict[str, str]) -> None:
    body = [XTB_HEADER, f'<translationbundle lang="{lang}">\n']
    for tid in sorted(translations.keys()):
        # No XML escaping for the body — translations may legitimately contain
        # `<ph name="X" />`. Other XML-special chars in human text are rare
        # enough that we punt; if it ever bites, swap to a proper escaper that
        # leaves <ph .../> alone.
        value = normalize_xtb_translation(translations[tid])
        body.append(f'<translation id="{tid}">{value}</translation>\n')
    body.append("</translationbundle>\n")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(body), encoding="utf-8")


# ---------- WebUI en.ts parsing & writing ----------

# Match `'key': 'value',` or `"key": "value"` plus multi-line concatenations
# of the form `'key':\n    'val1' +\n    'val2',`. We accept both quote styles.
TS_ENTRY_RE = re.compile(
    r"""
    ^[\t ]*['"]([^'"]+)['"]\s*:\s*  # key
    (?P<value>(?:'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"|\s*\+\s*)+)  # value
    ,?[\t ]*\r?$
    """,
    re.VERBOSE | re.MULTILINE,
)


def parse_ts_dict(ts_path: Path) -> Dict[str, str]:
    """Extract { key: value } from a Dictionary literal in a .ts file."""
    txt = ts_path.read_text(encoding="utf-8")
    # Locate the dict body — between `Dictionary = {` and the closing `};`.
    m = re.search(r"Dictionary\s*=\s*\{([\s\S]*?)\n\};", txt)
    if not m:
        raise ValueError(f"could not locate Dictionary literal in {ts_path}")
    body = m.group(1)
    out: Dict[str, str] = {}
    # Walk line by line to build (key, value) pairs that may span lines.
    pending_key: Optional[str] = None
    pending_value: List[str] = []
    for raw in body.splitlines():
        line = raw.rstrip()
        if not line.strip() or line.lstrip().startswith("//"):
            continue
        if pending_key is None:
            mm = re.match(
                r"\s*['\"]([^'\"]+)['\"]\s*:\s*(.*)$", line)
            if not mm:
                continue
            pending_key = mm.group(1)
            pending_value = [mm.group(2)]
        else:
            pending_value.append(line)
        joined = " ".join(pending_value)
        # Done when the joined fragment ends with a `,` (or `}`) outside a
        # quoted string. Approximate: count quotes of the first kind seen.
        if re.search(r",\s*$", joined) or re.search(r"\}\s*$", joined):
            # Strip trailing comma.
            jv = re.sub(r",\s*$", "", joined.strip())
            # Eval the JS expression: it is a string concat like 'a' + 'b'.
            try:
                value = _eval_js_string(jv)
            except Exception:
                value = jv
            out[pending_key] = value
            pending_key = None
            pending_value = []
    return out


def _eval_js_string(expr: str) -> str:
    """Best-effort evaluator for `'a' + 'b' + "c"` style expressions."""
    pieces = re.findall(
        r"'((?:\\.|[^'\\])*)'|\"((?:\\.|[^\"\\])*)\"", expr)
    parts: List[str] = []
    for a, b in pieces:
        s = a if a else b
        # Decode common escapes.
        s = (s.replace("\\n", "\n").replace("\\t", "\t")
             .replace("\\'", "'").replace('\\"', '"')
             .replace("\\\\", "\\"))
        parts.append(s)
    return "".join(parts)


def write_ts_locale(ts_path: Path, lang: str, translations: Dict[str, str],
                    en_keys_in_order: List[str]) -> None:
    """Write a locale .ts file. Keys with no translation fall back to en
    via the `...en` spread (which the file already includes), so we omit them
    from the explicit dict — that way 'translated' vs 'pending' is visible
    in source review."""
    lines = [
        "// Auto-translated by scripts/i18n-translate.py.",
        f"// Lang: {lang}. Hand-edit this file once and the script will skip",
        "// it next time (use --force to overwrite).",
        "import en from './en.js';",
        "import type { Dictionary } from '../i18n.js';",
        "",
        "const dict: Dictionary = {",
        "  ...en,",
    ]
    for k in en_keys_in_order:
        if k not in translations:
            continue
        v = translations[k]
        # Always single-quote with escaping.
        escaped = v.replace("\\", "\\\\").replace("'", "\\'")
        lines.append(f"  '{k}': '{escaped}',")
    lines.append("};")
    lines.append("")
    lines.append("export default dict;")
    ts_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---------- OpenAI client ----------

PROMPT_TEMPLATE = """You are a senior localization engineer translating a software UI from English into {lang_name} ({lang_code}).

Strict rules:
- Output ONLY a JSON object: {{ "key": "translation", ... }}.
- Keep every placeholder token EXACTLY as it appears in the source. Placeholder formats you may see:
  * `<ph name="X">...</ph>` — replace ENTIRELY with the SELF-CLOSING form `<ph name="X" />`. The inner text and the closing `</ph>` tag MUST NOT appear in your output. Wrong: `<ph name="X">⌘S</ph>` or `<ph name="X" />text</ph>`. Right: `<ph name="X" />`.
  * `{{name}}` — leave as `{{name}}` verbatim.
  * `$1`, `$2` — leave verbatim.
- Match register and concision of UI labels: do NOT add explanations, parentheses, or extra punctuation.
- For brand and product nouns ("Dao", "Control Center", "Skill", "Memory"), use the conventional translation in your locale's software ecosystem; if no convention exists, keep the English term.
- Preserve trailing ellipses, leading/trailing spaces, and case where it is meaningful (e.g. an all-caps label stays all-caps).
- For very short tokens like "OK", "Yes", "No", "Copy", use the standard locale equivalent (do not transliterate).

Source language: en
Target language: {lang_code} ({lang_name})

Source strings (JSON):
{payload}

Return ONLY the translated JSON object, no markdown fences or commentary.
"""


def call_openai(prompt: str, api_key: str, model: str) -> str:
    req = urllib.request.Request(
        f"{OPENAI_BASE_URL}/chat/completions",
        data=json.dumps({
            "model": model,
            "temperature": 0.2,
            "response_format": {"type": "json_object"},
            "messages": [{"role": "user", "content": prompt}],
        }).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        body = json.loads(resp.read())
    return body["choices"][0]["message"]["content"]


def translate_batch(strings: Dict[str, str], lang_code: str, lang_name: str,
                    api_key: str, model: str) -> Dict[str, str]:
    """Translate a batch of strings in one OpenAI call. Returns {key: text}."""
    prompt = PROMPT_TEMPLATE.format(
        lang_code=lang_code, lang_name=lang_name,
        payload=json.dumps(strings, ensure_ascii=False, indent=2))
    text = call_openai(prompt, api_key, model)
    try:
        data = json.loads(text)
    except json.JSONDecodeError as e:
        raise RuntimeError(
            f"openai returned non-json for {lang_code}: {text[:200]}") from e
    return {k: str(v) for k, v in data.items()}


# ---------- locale list ----------

def chromium_locales() -> List[Tuple[str, str]]:
    """The list of locales we mirror from Chromium. Source of truth lives in
    the bootstrap script's grd output; here we just enumerate the .xtb files
    we wrote and infer the lang code from the filename."""
    out: List[Tuple[str, str]] = []
    for p in sorted(XTB_DIR.glob("dao_strings_*.xtb")):
        lang = p.stem.replace("dao_strings_", "")
        out.append((lang, _lang_display_name(lang)))
    return out


# Minimal lang-code -> display name map. Falls back to the code itself if
# unknown — the OpenAI prompt also passes the bare code so the model still
# has enough signal.
LANG_NAMES = {
    "af": "Afrikaans", "am": "Amharic", "ar": "Arabic", "as": "Assamese",
    "az": "Azerbaijani", "be": "Belarusian", "bg": "Bulgarian",
    "bn": "Bengali", "bs": "Bosnian", "ca": "Catalan", "cs": "Czech",
    "cy": "Welsh", "da": "Danish", "de": "German", "el": "Greek",
    "en-GB": "British English", "es": "Spanish",
    "es-419": "Latin American Spanish", "et": "Estonian", "eu": "Basque",
    "fa": "Persian", "fi": "Finnish", "fil": "Filipino", "fr": "French",
    "fr-CA": "Canadian French", "gl": "Galician", "gu": "Gujarati",
    "he": "Hebrew", "hi": "Hindi", "hr": "Croatian", "hu": "Hungarian",
    "hy": "Armenian", "id": "Indonesian", "is": "Icelandic", "it": "Italian",
    "iw": "Hebrew", "ja": "Japanese", "ka": "Georgian", "kk": "Kazakh",
    "km": "Khmer", "kn": "Kannada", "ko": "Korean", "ky": "Kyrgyz",
    "lo": "Lao", "lt": "Lithuanian", "lv": "Latvian", "mk": "Macedonian",
    "ml": "Malayalam", "mn": "Mongolian", "mr": "Marathi", "ms": "Malay",
    "my": "Burmese", "ne": "Nepali", "nl": "Dutch",
    "no": "Norwegian Bokmål", "or": "Odia", "pa": "Punjabi", "pl": "Polish",
    "pt-BR": "Brazilian Portuguese", "pt-PT": "European Portuguese",
    "ro": "Romanian", "ru": "Russian", "si": "Sinhala", "sk": "Slovak",
    "sl": "Slovenian", "sq": "Albanian", "sr": "Serbian (Cyrillic)",
    "sr-Latn": "Serbian (Latin)", "sv": "Swedish", "sw": "Swahili",
    "ta": "Tamil", "te": "Telugu", "th": "Thai", "tr": "Turkish",
    "uk": "Ukrainian", "ur": "Urdu", "uz": "Uzbek", "vi": "Vietnamese",
    "zh-CN": "Simplified Chinese", "zh-HK": "Traditional Chinese (Hong Kong)",
    "zh-TW": "Traditional Chinese (Taiwan)", "zu": "Zulu",
}


def _lang_display_name(code: str) -> str:
    return LANG_NAMES.get(code, code)


# ---------- main ----------

def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--langs", help="Comma-separated locale list. Default: all "
                   "locales mirrored from Chromium.")
    p.add_argument("--force", action="store_true",
                   help="Re-translate even if a target file already has "
                   "non-empty translations.")
    p.add_argument("--only", choices=["grd", "webui"], help="Run only one "
                   "side. Default runs both.")
    p.add_argument("--model", default=OPENAI_MODEL, help="OpenAI model id.")
    p.add_argument("--dry-run", action="store_true",
                   help="Show what would be translated without calling "
                   "OpenAI or writing files.")
    p.add_argument("--jobs", type=int, default=4,
                   help="Number of locales to translate in parallel. "
                   "Default: 4. Set to 1 to run serially.")
    args = p.parse_args()

    api_key = os.environ.get("OPENAI_API_KEY")
    if not args.dry_run and not api_key:
        print("error: OPENAI_API_KEY is required (or pass --dry-run).",
              file=sys.stderr)
        sys.exit(2)

    if args.langs:
        wanted = [(c, _lang_display_name(c)) for c in args.langs.split(",")]
    else:
        wanted = chromium_locales()

    grd_messages = parse_grd_messages(GRD)
    en_dict = parse_ts_dict(WEBUI_LOCALES / "en.ts")
    en_keys = list(en_dict.keys())

    print(f"grd: {len(grd_messages)} messages; webui: {len(en_dict)} keys; "
          f"target locales: {len(wanted)}; jobs: {args.jobs}")

    jobs = max(1, args.jobs)
    if jobs == 1:
        for lang_code, lang_name in wanted:
            for line in translate_one_locale(
                    lang_code, lang_name, args, api_key,
                    grd_messages, en_dict, en_keys):
                print(line)
            # Be polite to the API when running serially.
            if not args.dry_run:
                time.sleep(0.2)
        return

    # Parallel mode: each worker collects its log lines and we flush them in
    # one chunk per locale so output stays grouped instead of interleaved.
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {
            pool.submit(
                translate_one_locale, lang_code, lang_name, args, api_key,
                grd_messages, en_dict, en_keys): lang_code
            for lang_code, lang_name in wanted
        }
        for fut in concurrent.futures.as_completed(futures):
            lang_code = futures[fut]
            try:
                for line in fut.result():
                    print(line)
            except Exception as e:
                print(f"[{lang_code}] FAILED: {e}", file=sys.stderr)


def translate_one_locale(lang_code: str, lang_name: str, args,
                         api_key: Optional[str],
                         grd_messages: List[Tuple[str, str, str]],
                         en_dict: Dict[str, str],
                         en_keys: List[str]) -> List[str]:
    """Process one locale end-to-end. Returns log lines instead of printing
    so callers in parallel mode can keep per-locale output grouped."""
    log: List[str] = []

    # ---- grd / xtb ----
    if args.only != "webui":
        xtb_path = XTB_DIR / f"dao_strings_{lang_code}.xtb"
        existing = read_existing_xtb(xtb_path)
        need: Dict[str, Tuple[str, str]] = {}  # key -> (id, source)
        for name, presentable, source in grd_messages:
            tid = str(GenerateMessageId(presentable))
            if not args.force and tid in existing:
                continue
            need[name] = (tid, source)
        if need:
            log.append(f"[{lang_code}/grd] translating "
                       f"{len(need)} messages...")
            if args.dry_run:
                for k, (tid, src) in need.items():
                    log.append(f"  {k} ({tid}): {src!r}")
            else:
                payload = {k: src for k, (tid, src) in need.items()}
                translated = translate_batch(
                    payload, lang_code, lang_name, api_key, args.model)
                new_xtb = dict(existing)
                for k, (tid, _src) in need.items():
                    if k in translated:
                        new_xtb[tid] = translated[k]
                write_xtb(xtb_path, lang_code, new_xtb)
                log.append(f"[{lang_code}/grd] wrote {xtb_path.name} "
                           f"({len(new_xtb)} entries)")
        else:
            log.append(f"[{lang_code}/grd] up-to-date")

    # ---- webui ts ----
    if args.only != "grd":
        ts_path = WEBUI_LOCALES / f"{lang_code}.ts"
        existing_ts = parse_ts_dict(ts_path) if ts_path.exists() else {}
        need_ts: Dict[str, str] = {}
        for k, v in en_dict.items():
            if (not args.force and k in existing_ts and
                    existing_ts[k] != v):
                # already overridden (translated)
                continue
            if not args.force and existing_ts.get(k) == v:
                # placeholder fallback to en — needs translation
                pass
            need_ts[k] = v
        # Drop keys already explicitly overridden when not --force.
        if not args.force:
            need_ts = {k: v for k, v in need_ts.items()
                       if k not in existing_ts or existing_ts[k] == v}
        if need_ts:
            log.append(f"[{lang_code}/webui] translating "
                       f"{len(need_ts)} entries...")
            if args.dry_run:
                for k, v in list(need_ts.items())[:5]:
                    log.append(f"  {k}: {v!r}")
                log.append("  ...")
            else:
                translated_ts = translate_batch(
                    need_ts, lang_code, lang_name, api_key, args.model)
                merged = {**existing_ts, **translated_ts}
                write_ts_locale(ts_path, lang_code, merged, en_keys)
                log.append(f"[{lang_code}/webui] wrote {ts_path.name} "
                           f"({len(merged)} entries)")
        else:
            log.append(f"[{lang_code}/webui] up-to-date")

    return log


if __name__ == "__main__":
    main()
