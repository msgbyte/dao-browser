import importlib.util
from pathlib import Path
import unittest


SPEC = importlib.util.spec_from_file_location(
    'windows_checkout', Path(__file__).parents[1] / 'configure-windows-checkout.py')
CHECKOUT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKOUT)


class WindowsCheckoutTest(unittest.TestCase):
    def test_preserves_other_settings_and_replaces_one_multiline_list(self):
        original = ('# Settings\r\nsolutions = []\r\ntarget_os = [\r\n'
                    '  "mac", # A comment with ]\r\n]\r\n'
                    'cache_dir = "cache"\r\n').encode()
        result = CHECKOUT.configure(original)
        self.assertIn(b'target_os = ["mac", "win"]\r\n', result)
        self.assertIn(b'# Settings\r\nsolutions = []\r\n', result)
        self.assertIn(b'cache_dir = "cache"\r\n', result)
        self.assertEqual(result.count(b'target_os ='), 1)
        self.assertEqual(CHECKOUT.configure(result), result)

    def test_adds_a_literal_list_to_new_configuration(self):
        result = CHECKOUT.configure(b'solutions = []\n')
        self.assertIn(b'target_os = ["win"]\n', result)
        self.assertEqual(CHECKOUT.configure(result), result)

    def test_keeps_an_existing_windows_list_and_unicode_settings(self):
        original = 'cache_dir = "\u96ea"\ntarget_os = ["mac", "win"] # retain\n'.encode()
        self.assertTrue(CHECKOUT.configure(original).startswith(original))

    def test_rejects_ambiguous_or_nonliteral_platforms(self):
        for value in [b'target_os = []\ntarget_os = ["win"]\n',
                      b'target_os = other\n', b'target_os = [42]\n']:
            with self.subTest(value=value), self.assertRaises(ValueError):
                CHECKOUT.configure(value)


if __name__ == '__main__':
    unittest.main()
