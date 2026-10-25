"""Check V8 metadata parsing with the imported, bundled libclang.

Run after `npm run import`: python3 -m unittest scripts.tests.test_windows_metagen
"""

import contextlib
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "engine/src"


@unittest.skipUnless(
    (SOURCE / "third_party/llvm-libclang/bindings/python/clang/cindex.py").is_file(),
    "Requires an imported Chromium checkout with bundled libclang")
class WindowsMetagenTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(SOURCE / "v8/tools"))
        from metagen import clang_bootstrap
        clang_bootstrap.bootstrap(str(SOURCE / "third_party/llvm-libclang"))
        from metagen import cpp_hier
        cls.cpp_hier = cpp_hier

    def test_absolute_driver_harvests_classes_with_relative_includes(self):
        # A checkout under /Users reproduces clang-cl's /U option collision.
        with tempfile.TemporaryDirectory(prefix="dao metagen ", dir=ROOT) as temp:
            root = Path(temp)
            build = root / "build"
            build.mkdir()
            header = root / "include/objects.h"
            header.parent.mkdir()
            header.write_text(
                "#ifndef DAO_METAGEN_FIXTURE\n#error Missing define\n#endif\n"
                "namespace v8 { namespace internal {\n"
                "class HeapObject {};\n"
                "class Example : public HeapObject {};\n"
                "}}\n", encoding="utf-8")
            driver = root / "harvest-driver.cc"
            driver.write_text('#include "objects.h"\n', encoding="utf-8")
            cwd = os.getcwd()
            for mode in ("cl", "g++"):
                with self.subTest(mode=mode):
                    flags = [f"--driver-mode={mode}", "-fsyntax-only",
                             "--target=x86_64-pc-windows-msvc", "-I../include",
                             "-DDAO_METAGEN_FIXTURE"]
                    # Failure diagnostics are checked separately; do not spawn clang.
                    with mock.patch.object(self.cpp_hier, "_dump_parse_failure"):
                        result = self.cpp_hier.scan_cpp(
                            str(root), str(driver), flags, parse_cwd=str(build))
                    self.assertEqual([c.name for c in result.classes],
                                     ["HeapObject", "Example"])
                    self.assertEqual(result.classes[1].base, "HeapObject")
                    self.assertIn(str(header), result.provenance)
                    self.assertEqual(os.getcwd(), cwd)

    def test_failure_diagnostics_protect_absolute_input_from_cl_options(self):
        driver = "/Users/dao checkout/v8/tools/metagen/harvest-driver.cc"
        flags = ["--driver-mode=cl", "-DDAO_METAGEN_FIXTURE", "-I../include"]
        with mock.patch.object(self.cpp_hier.subprocess, "run") as run, \
                contextlib.redirect_stderr(io.StringIO()):
            run.return_value.stderr = ""
            run.return_value.returncode = 0
            self.cpp_hier._dump_parse_failure(
                str(SOURCE / "v8"), driver, flags, "test parse failure")
        command = run.call_args.args[0]
        self.assertEqual(command[-2:], ["--", driver])
        for flag in flags:
            self.assertLess(command.index(flag), command.index("--"))


if __name__ == "__main__":
    unittest.main()
