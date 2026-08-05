#!/usr/bin/env python3
"""Run Dao desktop and Android translation workflows independently."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
DESKTOP_TRANSLATOR = ROOT / "scripts" / "i18n-translate.py"
ANDROID_TRANSLATOR = ROOT / "scripts" / "i18n_android.py"


def _shared_arguments(args: argparse.Namespace) -> list[str]:
    forwarded: list[str] = []
    if args.langs:
        forwarded.extend(["--langs", args.langs])
    if args.force:
        forwarded.append("--force")
    if args.model:
        forwarded.extend(["--model", args.model])
    if args.dry_run:
        forwarded.append("--dry-run")
    forwarded.extend(["--jobs", str(args.jobs)])
    return forwarded


def build_commands(
    args: argparse.Namespace,
    *,
    python: str = sys.executable,
) -> list[list[str]]:
    shared = _shared_arguments(args)
    commands: list[list[str]] = []
    if args.only in (None, "desktop", "grd", "webui"):
        desktop = [python, str(DESKTOP_TRANSLATOR), *shared]
        if args.only in ("grd", "webui"):
            desktop.extend(["--only", args.only])
        commands.append(desktop)
    if args.only in (None, "android"):
        commands.append([python, str(ANDROID_TRANSLATOR), *shared])
    return commands


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Translate Dao desktop and Android resources independently via OpenAI."
        )
    )
    parser.add_argument("--langs", help="Comma-separated locale list. Default: all configured locales.")
    parser.add_argument("--force", action="store_true", help="Overwrite existing translations.")
    parser.add_argument("--model", help="OpenAI model ID. Default: gpt-5.5.")
    parser.add_argument("--dry-run", action="store_true", help="Plan without API calls or file writes.")
    parser.add_argument("--jobs", type=int, default=4, help="Locales translated in parallel. Default: 4.")
    parser.add_argument(
        "--only",
        choices=["desktop", "android", "grd", "webui"],
        help="Run one platform or one desktop resource format.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    for command in build_commands(args):
        completed = subprocess.run(command, cwd=ROOT, check=False)
        if completed.returncode != 0:
            return completed.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
