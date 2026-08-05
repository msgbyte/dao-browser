from pathlib import Path
from tempfile import TemporaryDirectory
import os
import subprocess
import sys
import unittest

from scripts.i18n_android import (
    android_resource_qualifier,
    extract_android_placeholders,
    parse_android_strings,
    validate_translation,
    write_android_catalog,
)


class AndroidResourceParserTest(unittest.TestCase):
    def test_parses_values_placeholders_markup_and_translatable_flag(self) -> None:
        with TemporaryDirectory() as directory:
            source = Path(directory) / "strings.xml"
            source.write_text(
                """<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="plain">Search &amp; browse</string>
    <string name="formatted">Move %1$s to %2$d folders (100%%)</string>
    <string name="markup">Read <b>carefully</b></string>
    <string name="internal" translatable="false">dao://internal</string>
</resources>
""",
                encoding="utf-8",
            )

            entries = parse_android_strings(source)

        self.assertEqual(
            ["plain", "formatted", "markup", "internal"],
            [entry.name for entry in entries],
        )
        self.assertEqual("Search & browse", entries[0].value)
        self.assertEqual("Move %1$s to %2$d folders (100%%)", entries[1].value)
        self.assertEqual("Read <b>carefully</b>", entries[2].value)
        self.assertFalse(entries[3].translatable)

    def test_extracts_android_format_placeholders_in_source_order(self) -> None:
        self.assertEqual(
            ("%1$s", "%2$d", "%%", "%s"),
            extract_android_placeholders("Move %1$s to %2$d folders (100%%): %s"),
        )


class AndroidResourceQualifierTest(unittest.TestCase):
    def test_maps_language_region_and_script_tags_to_android_qualifiers(self) -> None:
        cases = {
            "ja": "ja",
            "zh-CN": "zh-rCN",
            "pt-BR": "pt-rBR",
            "en-GB": "en-rGB",
            "sr-Latn": "b+sr+Latn",
            "es-419": "b+es+419",
        }

        for locale_code, expected in cases.items():
            with self.subTest(locale_code=locale_code):
                self.assertEqual(expected, android_resource_qualifier(locale_code))


class AndroidTranslationValidationTest(unittest.TestCase):
    def test_rejects_missing_extra_and_mutated_placeholders(self) -> None:
        source = {
            "title": "Move %1$s",
            "count": "%1$d tabs",
            "markup": "Read <b>carefully</b>",
        }

        invalid_catalogs = [
            {"title": "Déplacer %1$s", "count": "%1$d onglets"},
            {
                "title": "Déplacer %1$s",
                "count": "%1$d onglets",
                "markup": "Lire <b>attentivement</b>",
                "extra": "Non",
            },
            {
                "title": "Déplacer %2$s",
                "count": "%1$d onglets",
                "markup": "Lire <b>attentivement</b>",
            },
            {
                "title": "Déplacer %1$s",
                "count": "%1$d onglets",
                "markup": "Lire attentivement",
            },
        ]

        for translated in invalid_catalogs:
            with self.subTest(translated=translated):
                with self.assertRaises(ValueError):
                    validate_translation(source, translated)

    def test_writes_a_complete_parseable_catalog_atomically(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source_path = root / "source.xml"
            source_path.write_text(
                """<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="plain">Search &amp; browse</string>
    <string name="formatted">Move %1$s</string>
    <string name="markup">Read <b>carefully</b></string>
    <string name="internal" translatable="false">dao://internal</string>
</resources>
""",
                encoding="utf-8",
            )
            source_entries = parse_android_strings(source_path)
            destination = root / "values-fr" / "strings.xml"

            write_android_catalog(
                destination,
                source_entries,
                {
                    "plain": "Rechercher & naviguer",
                    "formatted": "Déplacer %1$s",
                    "markup": "Lire <b>attentivement</b>",
                },
            )

            written = parse_android_strings(destination)

        self.assertEqual(
            [
                "Rechercher & naviguer",
                "Déplacer %1$s",
                "Lire <b>attentivement</b>",
                "dao://internal",
            ],
            [entry.value for entry in written],
        )
        self.assertFalse(written[-1].translatable)

    def test_dry_run_needs_no_api_key_and_writes_no_catalog(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "values" / "strings.xml"
            source.parent.mkdir(parents=True)
            source.write_text(
                """<?xml version="1.0" encoding="utf-8"?>
<resources><string name="hello">Hello</string></resources>
""",
                encoding="utf-8",
            )
            env = dict(os.environ)
            env.pop("OPENAI_API_KEY", None)
            env["OPENAI_BASE_URL"] = "http://127.0.0.1:1"

            result = subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "scripts.i18n_android",
                    "--source",
                    str(source),
                    "--resources-root",
                    str(root),
                    "--langs",
                    "ja",
                    "--dry-run",
                    "--jobs",
                    "1",
                ],
                cwd=Path(__file__).resolve().parents[2],
                env=env,
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("[ja/android] would translate 1 strings", result.stdout)
            self.assertFalse((root / "values-ja" / "strings.xml").exists())


if __name__ == "__main__":
    unittest.main()
