#!/usr/bin/env bash
# Translate Dao i18n source strings into every locale via OpenAI.
#
# Reads `OPENAI_API_KEY` (and optional `OPENAI_BASE_URL`,
# `OPENAI_TRANSLATE_MODEL`) from the environment. Default model: gpt-4o.
#
# Forwards all flags to scripts/i18n-translate.py — see that file's
# docstring for the full option list. Common invocations:
#
#   OPENAI_API_KEY=sk-... sh scripts/i18n-translate.sh
#   OPENAI_API_KEY=sk-... sh scripts/i18n-translate.sh --langs zh-TW,ja
#   OPENAI_API_KEY=sk-... sh scripts/i18n-translate.sh --force --only grd
#   OPENAI_API_KEY=sk-... sh scripts/i18n-translate.sh --dry-run

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 "$ROOT/scripts/i18n-translate.py" "$@"
