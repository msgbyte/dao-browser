#!/bin/sh
set -eu

usage() {
  cat <<'EOF'
Usage:
  sh scripts/fix-import-patches.sh <src/patches/...patch> [...]

For each patch path, this resets every tracked engine/src target file to
Chromium HEAD, removes exact untracked targets declared as new files, then
applies the current Dao patch. It is intended for import failures where
engine/src already has an older version of the same patch.
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

validate_target_parents() {
  check_target=$1
  check_parent=${check_target%/*}
  if [ "$check_parent" = "$check_target" ]; then
    return
  fi

  check_current=$ENGINE_SRC
  check_remaining=$check_parent
  while [ -n "$check_remaining" ]; do
    check_component=${check_remaining%%/*}
    check_current=$check_current/$check_component
    if [ -L "$check_current" ]; then
      echo "error: patch target has a symlink parent: $check_target" >&2
      exit 1
    fi
    if [ -e "$check_current" ] && [ ! -d "$check_current" ]; then
      echo "error: patch target has a non-directory parent: $check_target" >&2
      exit 1
    fi
    case "$check_remaining" in
      */*) check_remaining=${check_remaining#*/} ;;
      *) check_remaining="" ;;
    esac
  done
}

targets_file=$(mktemp "${TMPDIR:-/tmp}/dao-fix-import-targets.XXXXXX")
trap 'rm -f "$targets_file"' 0 HUP INT TERM

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

  if git -C "$ENGINE_SRC" apply --check --reverse "$patch_abs" >/dev/null 2>&1; then
    echo "already current: $patch_rel"
    continue
  fi

  awk '
    /^diff --git / {
      old = ""
      new_file = 0
      in_header = 1
      next
    }
    in_header && /^--- \/dev\/null$/ {
      old = ""
      new_file = 1
      next
    }
    in_header && /^--- a\// {
      old = substr($0, 7)
      new_file = 0
      next
    }
    in_header && /^\+\+\+ b\// {
      kind = new_file ? "new" : "tracked"
      print kind "\t" substr($0, 7)
      in_header = 0
      next
    }
    in_header && /^\+\+\+ \/dev\/null$/ {
      if (old != "") {
        print "tracked\t" old
      }
      in_header = 0
    }
  ' "$patch_abs" >"$targets_file"

  if [ ! -s "$targets_file" ]; then
    echo "error: could not determine patch targets: $input_path" >&2
    exit 1
  fi

  echo "repairing: $patch_rel"
  tab=$(printf '\t')
  while IFS="$tab" read -r target_kind target_rel; do
    case "$target_rel" in
      ""|/*|..|../*|*/../*|*/..|./*|*/./*)
        echo "error: unsafe patch target: $target_rel" >&2
        exit 1
        ;;
    esac

    validate_target_parents "$target_rel"

    case "$target_kind" in
      new)
        if git -C "$ENGINE_SRC" ls-files --error-unmatch "$target_rel" \
            >/dev/null 2>&1; then
          echo "error: patch new-file target is tracked: $target_rel" >&2
          exit 1
        fi
        if [ -d "$ENGINE_SRC/$target_rel" ]; then
          echo "error: patch new-file target is a directory: $target_rel" >&2
          exit 1
        fi
        rm -f "$ENGINE_SRC/$target_rel"
        ;;
      tracked)
        if git -C "$ENGINE_SRC" ls-files --error-unmatch "$target_rel" \
            >/dev/null 2>&1; then
          git -C "$ENGINE_SRC" checkout -- "$target_rel"
        else
          echo "error: patch target is not tracked: $target_rel" >&2
          exit 1
        fi
        ;;
      *)
        echo "error: unknown patch target kind: $target_kind" >&2
        exit 1
        ;;
    esac
  done <"$targets_file"

  git -C "$ENGINE_SRC" apply "$patch_abs"
done

rm -f "$targets_file"
trap - 0 HUP INT TERM

echo "done. Re-run: npm run import"
