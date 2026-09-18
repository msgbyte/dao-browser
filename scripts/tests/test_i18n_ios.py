from pathlib import Path
from tempfile import TemporaryDirectory
import os
import subprocess
import sys
import unittest

from scripts.i18n_ios import (
    extract_ios_placeholders,
    lproj_name,
    parse_strings,
    parse_strings_text,
    validate_translation,
    write_strings,
)


ROOT = Path(__file__).resolve().parents[2]


class IosStringsTest(unittest.TestCase):
    def test_parses_comments_escapes_and_urls(self) -> None:
        entries = parse_strings_text(
            '/* Header */\n'
            '"plain" = "Search & browse";\n'
            '// Line comment\n'
            '"escaped" = "Say \\"hi\\"\\nnext";\n'
            '"url" = "https://dao.example/path";\n'
        )

        self.assertEqual(
            {"plain": "Search & browse", "escaped": 'Say "hi"\nnext', "url": "https://dao.example/path"},
            entries,
        )

    def test_rejects_unrecognized_content(self) -> None:
        with self.assertRaises(ValueError):
            parse_strings_text('"broken" = "missing semicolon"\n')

    def test_extracts_ios_placeholders(self) -> None:
        self.assertEqual(
            ("%@", "%1$@", "%lld", "%.1f", "%%"),
            extract_ios_placeholders("%@ %1$@ %lld %.1f 100%%"),
        )

    def test_maps_chromium_locales_to_lproj_names(self) -> None:
        self.assertEqual(
            ["zh-Hans", "zh-Hant", "zh-HK", "he", "nb", "pt-BR", "ja"],
            [lproj_name(code) for code in ["zh-CN", "zh-TW", "zh-HK", "iw", "no", "pt-BR", "ja"]],
        )

    def test_rejects_missing_keys_and_mutated_placeholders(self) -> None:
        source = {"count": "%d tabs", "name": "Open %@"}
        with self.assertRaises(ValueError):
            validate_translation(source, {"count": "%d 个标签页"})
        with self.assertRaises(ValueError):
            validate_translation(source, {"count": "%d 个标签页", "name": "打开"})
        validate_translation(source, {"count": "%d 个标签页", "name": "打开 %@"})

    def test_writes_round_trippable_table_in_source_order(self) -> None:
        source = {"b": "B", "a": "A"}
        translations = {"a": 'Quote "x"\\', "b": "Line\nbreak"}
        with TemporaryDirectory() as directory:
            destination = Path(directory) / "ja.lproj" / "Localizable.strings"
            write_strings(destination, source, translations)

            self.assertEqual(translations, parse_strings(destination))
            self.assertTrue(destination.read_text(encoding="utf-8").startswith('"b"'))
            self.assertEqual(["Localizable.strings"], os.listdir(destination.parent))

    def test_dry_run_needs_no_api_key_and_writes_nothing(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "en.lproj").mkdir()
            (root / "en.lproj" / "Localizable.strings").write_text('"hello" = "Hello";\n', encoding="utf-8")
            (root / "en.lproj" / "InfoPlist.strings").write_text('"CFBundleDisplayName" = "Dao";\n', encoding="utf-8")
            env = dict(os.environ)
            env.pop("OPENAI_API_KEY", None)
            env.pop("OPENAPI_KEY", None)
            env["OPENAI_BASE_URL"] = "http://127.0.0.1:1"

            result = subprocess.run(
                [sys.executable, "scripts/i18n_ios.py", "--resources-root", str(root),
                 "--langs", "zh-TW", "--dry-run", "--jobs", "1"],
                cwd=ROOT,
                env=env,
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("[zh-TW/ios] zh-Hant.lproj/Localizable.strings would translate 1 strings", result.stdout)
            self.assertFalse((root / "zh-Hant.lproj").exists())


if __name__ == "__main__":
    unittest.main()
