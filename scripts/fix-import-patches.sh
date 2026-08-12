#!/bin/sh
set -eu

usage() {
  cat <<'EOF'
Usage:
  sh scripts/fix-import-patches.sh <src/patches/...patch> [...]

For each patch path, this resets every tracked engine/src target file to
Chromium HEAD, removes exact untracked targets declared as new files, then
applies the current Dao patch. It is intended for import failures where
engine/src already has an older version of the same patch. If any repair
fails, all target paths are restored to their state before this command.
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

parse_patch_targets() {
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
  ' "$1"
}

state_dir=$(mktemp -d "${TMPDIR:-/tmp}/dao-fix-import-state.XXXXXX")
patches_file=$state_dir/patches
all_targets_file=$state_dir/all-targets
patch_targets_file=$state_dir/patch-targets
backup_dir=$state_dir/backup
mkdir -p "$backup_dir"
: >"$patches_file"
: >"$all_targets_file"

repair_started=0
repair_succeeded=0
tab=$(printf '\t')

restore_targets() {
  restore_failed=0
  while IFS="$tab" read -r target_kind target_rel; do
    target_path=$ENGINE_SRC/$target_rel
    backup_path=$backup_dir/$target_rel

    if [ -d "$target_path" ] && [ ! -L "$target_path" ]; then
      echo "error: cannot restore patch target over directory: $target_rel" >&2
      restore_failed=1
      continue
    fi
    if ! rm -f "$target_path"; then
      echo "error: could not clear patch target during restore: $target_rel" >&2
      restore_failed=1
      continue
    fi

    if [ -e "$backup_path" ] || [ -L "$backup_path" ]; then
      target_parent=${target_path%/*}
      if ! mkdir -p "$target_parent" ||
          ! cp -pP "$backup_path" "$target_path"; then
        echo "error: could not restore patch target: $target_rel" >&2
        restore_failed=1
      fi
    fi
  done <"$all_targets_file"

  [ "$restore_failed" -eq 0 ]
}

cleanup() {
  status=$?
  trap - 0 HUP INT TERM

  if [ "$repair_started" -eq 1 ] && [ "$repair_succeeded" -ne 1 ]; then
    echo "repair failed; restoring original patch targets..." >&2
    if restore_targets; then
      echo "restored original patch targets" >&2
    else
      echo "error: failed to restore one or more patch targets" >&2
      status=1
    fi
  fi

  rm -rf "$state_dir"
  exit "$status"
}

trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

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

  parse_patch_targets "$patch_abs" >"$patch_targets_file"

  if [ ! -s "$patch_targets_file" ]; then
    echo "error: could not determine patch targets: $input_path" >&2
    exit 1
  fi

  printf '%s\t%s\n' "$patch_abs" "$patch_rel" >>"$patches_file"
  cat "$patch_targets_file" >>"$all_targets_file"
done

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
      ;;
    tracked)
      if ! git -C "$ENGINE_SRC" ls-files --error-unmatch "$target_rel" \
          >/dev/null 2>&1; then
        echo "error: patch target is not tracked: $target_rel" >&2
        exit 1
      fi
      ;;
    *)
      echo "error: unknown patch target kind: $target_kind" >&2
      exit 1
      ;;
  esac

  target_path=$ENGINE_SRC/$target_rel
  backup_path=$backup_dir/$target_rel
  if [ -e "$target_path" ] || [ -L "$target_path" ]; then
    if [ -d "$target_path" ] && [ ! -L "$target_path" ]; then
      echo "error: patch target is a directory: $target_rel" >&2
      exit 1
    fi
    backup_parent=${backup_path%/*}
    mkdir -p "$backup_parent"
    cp -pP "$target_path" "$backup_path"
  fi
done <"$all_targets_file"

repair_started=1

while IFS="$tab" read -r patch_abs patch_rel; do
  if git -C "$ENGINE_SRC" apply --check --reverse "$patch_abs" \
      >/dev/null 2>&1; then
    echo "already current: $patch_rel"
    continue
  fi

  parse_patch_targets "$patch_abs" >"$patch_targets_file"

  echo "repairing: $patch_rel"
  while IFS="$tab" read -r target_kind target_rel; do
    case "$target_kind" in
      new) rm -f "$ENGINE_SRC/$target_rel" ;;
      tracked) git -C "$ENGINE_SRC" checkout -- "$target_rel" ;;
    esac
  done <"$patch_targets_file"

  git -C "$ENGINE_SRC" apply "$patch_abs"
done <"$patches_file"

while IFS="$tab" read -r patch_abs patch_rel; do
  if ! git -C "$ENGINE_SRC" apply --check --reverse "$patch_abs" \
      >/dev/null 2>&1; then
    echo "error: repaired patch did not verify: $patch_rel" >&2
    exit 1
  fi
done <"$patches_file"

repair_succeeded=1
echo "done. Re-run: npm run import"
