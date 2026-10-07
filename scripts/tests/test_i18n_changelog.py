import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from scripts import i18n_changelog as changelog


class ChangelogTranslationTest(unittest.TestCase):
    def test_incremental_translation_survives_archival_and_protects_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = '# Changelog\n\n## [desktop] Desktop\n\n### [Unreleased] Unreleased\n\n- Open `Settings`.\n'
            (root / 'en.md').write_text(source)
            (root / 'zh-CN.md').write_text('Hand-authored Chinese')
            def translate(prompt, *_):
                entries = json.loads(prompt.split('Source strings:\n')[1])
                return json.dumps({key: 'Translated ' + value for key, value in entries.items()})
            with patch.object(changelog.android, 'call_openai', side_effect=translate) as api:
                changelog.process_locale('ja', root, False, True, None, 'test')
                self.assertFalse((root / 'ja.md').exists())
                api.assert_not_called()
                changelog.process_locale('ja', root, False, False, 'key', 'test')
                self.assertIn('## [desktop] Translated Desktop', (root / 'ja.md').read_text())
                api.reset_mock()
                source = source.replace('- Open', '### [1.0.1] - 2026-10-07\n\n- Open')
                (root / 'en.md').write_text(source)
                changelog.process_locale('ja', root, False, False, 'key', 'test')
                api.assert_not_called()
                self.assertIn('### [1.0.1] - 2026-10-07', (root / 'ja.md').read_text())
                (root / 'en.md').write_text(source.replace('Settings', 'History'))
                changelog.process_locale('ja', root, False, False, 'key', 'test')
                self.assertEqual(api.call_count, 1)
                self.assertIn('`History`', (root / 'ja.md').read_text())
                before = (root / 'ja.md').read_text()
                with patch.object(changelog.android, 'call_openai', return_value='{}'):
                    with self.assertRaises(ValueError):
                        changelog.process_locale('ja', root, True, False, 'key', 'test')
                self.assertEqual(before, (root / 'ja.md').read_text())
                for locale in ('en', 'zh-CN'):
                    original = (root / f'{locale}.md').read_text()
                    changelog.process_locale(locale, root, True, False, 'key', 'test')
                    self.assertEqual(original, (root / f'{locale}.md').read_text())


if __name__ == '__main__':
    unittest.main()
