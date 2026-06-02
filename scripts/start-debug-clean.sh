#!/bin/sh
set -eu

PROFILE="$(mktemp -d /tmp/dao-debug.XXXXXX)"
echo "$PROFILE"

exec "engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug" \
  --user-data-dir="$PROFILE" \
  --use-mock-keychain \
  --no-first-run \
  --enable-logging=stderr \
  --v=0
