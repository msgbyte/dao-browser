#!/usr/bin/env bash
# Convenience entry point at the repo root for the i18n translator.
# Delegates to scripts/i18n-translate.sh. See that script's header for the
# full flag list. Common invocations:
#
#   OPENAI_API_KEY=sk-... sh ./i18n.sh
#   OPENAI_API_KEY=sk-... sh ./i18n.sh --langs zh-TW,ja
#   OPENAI_API_KEY=sk-... sh ./i18n.sh --dry-run

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
exec "$ROOT/scripts/i18n-translate.sh" "$@"
