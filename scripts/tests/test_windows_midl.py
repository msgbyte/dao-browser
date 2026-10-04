"""Check imported MIDL validation without launching MIDL or a Chromium build.

Run after `npm run import`: python3 -m unittest scripts.tests.test_windows_midl
"""

import contextlib
import io
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest import mock
import warnings


ROOT = Path(__file__).resolve().parents[2]
MIDL = ROOT / "engine/src/build/toolchain/win/midl.py"
CHROMIUM_IID = "A3FD580A-FFD4-4075-9174-75D0B199D3CB"
DAO_IID = "4BA68542-FD30-5412-B656-B7E6E6795EA7"


@unittest.skipUnless(MIDL.is_file(), "Requires an imported Chromium checkout")
class WindowsMidlTest(unittest.TestCase):
    def validate_outputs(self, *, dynamic=True, generated_baseline=False,
                         changed_output=None):
        midl = runpy.run_path(str(MIDL))
        with tempfile.TemporaryDirectory(prefix="dao-midl-") as directory, \
                warnings.catch_warnings():
            # Upstream midl.py uses open(...).read()/write() expressions.
            warnings.filterwarnings("ignore", category=ResourceWarning,
                                    module=r"<run_path>")
            root = Path(directory)
            baseline = root / "baseline" / "x64"
            baseline.mkdir(parents=True)
            output = root / "gen"
            output.mkdir()
            compiled = root / "compiled"
            compiled.mkdir()
            idl = root / "service.idl"
            idl.write_text(CHROMIUM_IID, encoding="utf-8")
            arch = root / "environment.x64"
            arch.write_bytes(b"PATH=unused\0\0")
            source_path = idl.as_posix()
            generated_path = (output / "service.idl").as_posix()
            baseline_path = generated_path if generated_baseline else source_path
            compiled_path = generated_path if dynamic else source_path
            settings = "/* Compiler settings for {}:\r\n    Oicf, W1\r\n */\r\n"
            source_header = settings.format(baseline_path) + CHROMIUM_IID + "\r\n"
            compiled_header = settings.format(compiled_path) + (
                DAO_IID if dynamic else CHROMIUM_IID) + "\r\n"
            source_iid = (
                "0xA3FD580A,0xFFD4,0x4075,0x91,0x74,0x75,0xD0,0xB1,0x99,0xD3,0xCB\r\n")
            compiled_iid = (
                "0x4BA68542,0xFD30,0x5412,0xB6,0x56,0xB7,0xE6,0xE6,0x79,0x5E,0xA7\r\n"
                if dynamic else source_iid)
            expected = {
                "service.h": compiled_header,
                "service_i.c": settings.format(compiled_path) + compiled_iid,
                "service_p.c": settings.format(compiled_path),
            }
            original = {
                "service.h": source_header,
                "service_i.c": settings.format(baseline_path) + source_iid,
                "service_p.c": settings.format(baseline_path),
            }
            for name, contents in original.items():
                (baseline / name).write_bytes(contents.encode())
            for name, contents in expected.items():
                if changed_output and name == changed_output[0]:
                    contents = contents.replace(*changed_output[1:])
                (compiled / name).write_bytes(contents.encode())

            # Substitute only the compiler process; exercise real file copying,
            # GUID replacement, IDL generation, and output comparison.
            with mock.patch.dict(midl["main"].__globals__, {
                "run_midl": lambda args, env: (0, str(compiled)),
            }), mock.patch.object(midl["sys"], "platform", "win32"), \
                    contextlib.redirect_stdout(io.StringIO()):
                result = midl["main"](
                    str(arch), str(baseline.parent), output.as_posix(),
                    f"{CHROMIUM_IID}={DAO_IID}" if dynamic else "none",
                    "none", "service.h", "none", "service_i.c", "service_p.c",
                    "unused-clang", source_path)

            # Normalization must never change checked-in baseline files.
            for name, contents in original.items():
                self.assertEqual((baseline / name).read_bytes(), contents.encode())
            if result == 0:
                for name, contents in expected.items():
                    self.assertEqual((output / name).read_bytes(), contents.encode())
            return result

    def test_dynamic_guids_accept_source_path_comments(self):
        self.assertEqual(self.validate_outputs(), 0)

    def test_existing_generated_and_non_dynamic_baselines_still_validate(self):
        self.assertEqual(self.validate_outputs(generated_baseline=True), 0)
        self.assertEqual(self.validate_outputs(dynamic=False), 0)

    def test_substantive_output_differences_still_fail(self):
        for change in [
            ("service.h", DAO_IID, CHROMIUM_IID),
            ("service_i.c", "0x4BA68542", "0x00000000"),
            ("service.h", "Oicf, W1", "Oicf, W2"),
            ("service.h", "service.idl:", "other.idl:"),
        ]:
            with self.subTest(change=change):
                self.assertEqual(self.validate_outputs(changed_output=change), 1)


if __name__ == "__main__":
    unittest.main()
