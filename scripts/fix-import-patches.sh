#!/bin/sh
set -eu

usage() {
  cat <<'EOF'
Usage:
  sh scripts/fix-import-patches.sh <src/patches/...patch> [...]

For each patch path, this resets only the matching engine/src target file to
Chromium HEAD, then applies the current Dao patch. It is intended for import
failures where engine/src already has an older version of the same patch.
EOF
}

if [ "$#" -eq 0 ]; then
  usage
  exit 2
fi

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PATCHES_DIR="$ROOT_DIR/src/patches"
ENGINE_SRC="$ROOT_DIR/engine/src"

if [ ! -d "$ENGINE_SRC/.git" ]; then
  echo "error: engine/src git checkout not found at $ENGINE_SRC" >&2
  exit 1
fi

for input_path in "$@"; do
  case "$input_path" in
    /*) patch_path="$input_path" ;;
    *) patch_path="$ROOT_DIR/$input_path" ;;
  esac

  if [ ! -f "$patch_path" ]; then
    echo "error: patch file not found: $input_path" >&2
    exit 1
  fi

  patch_dir=$(dirname -- "$patch_path")
  patch_base=$(basename -- "$patch_path")
  patch_abs=$(CDPATH= cd -- "$patch_dir" && pwd)/$patch_base

  case "$patch_abs" in
    "$PATCHES_DIR"/*) patch_rel=${patch_abs#"$PATCHES_DIR"/} ;;
    *)
      echo "error: patch must live under src/patches: $input_path" >&2
      exit 1
      ;;
  esac

  case "$patch_rel" in
    *.patch) ;;
    *)
      echo "error: patch path must end with .patch: $input_path" >&2
      exit 1
      ;;
  esac

  target_rel=$(
    awk '
      /^\+\+\+ b\// {
        print substr($0, 7)
        found = 1
        exit
      }
      /^\-\-\- a\// {
        old = substr($0, 7)
      }
      END {
        if (!found && old != "") {
          print old
        }
      }
    ' "$patch_abs"
  )
  if [ -z "$target_rel" ]; then
    echo "error: could not determine patch target: $input_path" >&2
    exit 1
  fi

  if git -C "$ENGINE_SRC" apply --check --reverse "$patch_abs" >/dev/null 2>&1; then
    echo "already current: $patch_rel"
    continue
  fi

  echo "repairing: $patch_rel -> engine/src/$target_rel"
  if git -C "$ENGINE_SRC" ls-files --error-unmatch "$target_rel" >/dev/null 2>&1; then
    git -C "$ENGINE_SRC" checkout -- "$target_rel"
  else
    rm -f "$ENGINE_SRC/$target_rel"
  fi
  git -C "$ENGINE_SRC" apply "$patch_abs"
done

echo "done. Re-run: npm run import"
