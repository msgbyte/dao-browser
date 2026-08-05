#!/bin/sh
# Repository-level entry point for independent desktop and Android translators.
# Common invocations:
#
#   OPENAI_API_KEY=sk-... sh ./i18n.sh
#   OPENAI_API_KEY=sk-... sh ./i18n.sh --langs zh-TW,ja
#   OPENAI_API_KEY=sk-... sh ./i18n.sh --only android
#   OPENAI_API_KEY=sk-... sh ./i18n.sh --dry-run

set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$ROOT/scripts/i18n.py" "$@"
