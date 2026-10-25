#!/usr/bin/env python3
# Copyright 2026 Dao Browser Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Exercise built dao-mcp and dao-browser binaries without a running browser.

Usage: python stdio_smoke_test.py /path/to/dao-mcp /path/to/dao-browser
Run on each target platform, including Windows, after npm run rebuild.
"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


def message(method, request_id=None, params=None):
    value = {"jsonrpc": "2.0", "method": method}
    if request_id is not None:
        value["id"] = request_id
    if params is not None:
        value["params"] = params
    return json.dumps(value, ensure_ascii=False).encode("utf-8") + b"\n"


class StdioSmokeTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="dao-mcp-test-")
        self.addCleanup(self.directory.cleanup)
        self.profile = Path(self.directory.name) / "配置 with spaces"
        self.profile.mkdir()

    def command(self, executable, *args):
        return [executable, *args, f"--user-data-dir={self.profile}"]

    def exchange(self, executable, data, *args):
        completed = subprocess.run(
            self.command(executable, *args), input=data,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertNotIn(b"\r", completed.stdout)
        return [json.loads(line) for line in completed.stdout.splitlines()]

    def test_initialize_unicode_ping_and_crlf(self):
        data = message("initialize", "初始化", {
            "protocolVersion": "2025-11-25", "capabilities": {},
            "clientInfo": {"name": "测试 agent", "version": "1.0"},
        })
        data += message("notifications/initialized")
        data += message("ping", "你好")
        for executable, args in [(HELPER, ()), (CLI, ("mcp",))]:
            with self.subTest(executable=executable):
                responses = self.exchange(
                    executable, data.replace(b"\n", b"\r\n"), *args)
                self.assertEqual([item["id"] for item in responses],
                                 ["初始化", "你好"])
                self.assertEqual(responses[0]["result"]["protocolVersion"],
                                 "2025-11-25")
                self.assertEqual(responses[1]["result"], {})

    def test_unterminated_input(self):
        response, = self.exchange(HELPER, message("ping", 1).rstrip(b"\n"))
        self.assertEqual(response["error"]["code"], -32600)

    def test_full_output_pipe_does_not_block_input_or_exit(self):
        # Never drain stdout: a blocking WriteFile would hang before queue limits.
        with tempfile.TemporaryFile() as source:
            source.write(message("ping", "x" * 1024) * 10000)
            source.seek(0)
            process = subprocess.Popen(
                self.command(HELPER), stdin=source,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            try:
                self.assertEqual(process.wait(timeout=10), 1)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                process.stdout.close()
                process.stderr.close()

    def test_cli_unicode_paths_and_help(self):
        completed = subprocess.run([CLI, "--help"], capture_output=True, timeout=10)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        completed = subprocess.run(
            [CLI, "skill", "--lite", "--user-data-dir", str(self.profile)],
            capture_output=True, timeout=10,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertTrue((self.profile / "dao_browser_guide_version").is_file())
        script = self.profile / "空 script.js"
        script.write_text("\n", encoding="utf-8")
        completed = subprocess.run(
            self.command(CLI, "run", str(script), "--no-launch"),
            capture_output=True, timeout=10,
        )
        self.assertEqual(completed.returncode, 64)
        self.assertIn(b"the script is empty", completed.stderr)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    HELPER, CLI = (str(Path(argument).resolve()) for argument in sys.argv[1:])
    unittest.main(argv=[sys.argv[0]])
