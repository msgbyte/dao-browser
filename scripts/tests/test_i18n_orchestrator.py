import argparse
from pathlib import Path
import sys
import unittest

from scripts.i18n import build_commands


ROOT = Path(__file__).resolve().parents[2]
DESKTOP = str(ROOT / "scripts" / "i18n-translate.py")
ANDROID = str(ROOT / "scripts" / "i18n_android.py")


def arguments(
    *,
    only: str | None = None,
    langs: str | None = None,
    force: bool = False,
    model: str | None = None,
    dry_run: bool = False,
    jobs: int = 4,
) -> argparse.Namespace:
    return argparse.Namespace(
        only=only,
        langs=langs,
        force=force,
        model=model,
        dry_run=dry_run,
        jobs=jobs,
    )


class I18nOrchestratorTest(unittest.TestCase):
    def test_builds_independent_desktop_and_android_commands(self) -> None:
        commands = build_commands(
            arguments(langs="zh-CN,ja", dry_run=True, jobs=1),
            python=sys.executable,
        )

        shared = ["--langs", "zh-CN,ja", "--dry-run", "--jobs", "1"]
        self.assertEqual(
            [
                [sys.executable, DESKTOP, *shared],
                [sys.executable, ANDROID, *shared],
            ],
            commands,
        )

    def test_limits_runs_to_one_platform_without_leaking_platform_options(self) -> None:
        android = build_commands(
            arguments(only="android", force=True, model="gpt-test"),
            python=sys.executable,
        )
        desktop = build_commands(
            arguments(only="desktop", force=True, model="gpt-test"),
            python=sys.executable,
        )

        shared = ["--force", "--model", "gpt-test", "--jobs", "4"]
        self.assertEqual([[sys.executable, ANDROID, *shared]], android)
        self.assertEqual([[sys.executable, DESKTOP, *shared]], desktop)

    def test_routes_grd_and_webui_only_to_the_desktop_translator(self) -> None:
        grd = build_commands(arguments(only="grd"), python=sys.executable)
        webui = build_commands(arguments(only="webui"), python=sys.executable)

        self.assertEqual(
            [[sys.executable, DESKTOP, "--jobs", "4", "--only", "grd"]],
            grd,
        )
        self.assertEqual(
            [[sys.executable, DESKTOP, "--jobs", "4", "--only", "webui"]],
            webui,
        )


if __name__ == "__main__":
    unittest.main()
