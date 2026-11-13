#!/usr/bin/env python3
"""Exercise a built Home CLI against an isolated Unix IPC test endpoint.

Usage: python3 scripts/tests/test-home-cli.py /absolute/path/to/dao-browser
This script does not build or launch a browser and never uses real profile data.
"""

import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest


CLI = str(Path(sys.argv.pop(1)).resolve())


def digest(files):
    value = hashlib.sha256()
    for name, contents in sorted(files.items()):
        for part in (name.encode(), contents.encode()):
            value.update(len(part).to_bytes(8, "big"))
            value.update(part)
    return value.hexdigest()


@unittest.skipUnless(os.name == "posix", "requires Unix IPC")
class HomeCliTest(unittest.TestCase):
    def setUp(self):
        # Keep the path below the Unix socket length limit, including on macOS.
        self.profile = Path(tempfile.mkdtemp(prefix="dao-cli-", dir="/tmp")).resolve()
        self.runtime = self.profile / "HomeCLI"
        self.runtime.mkdir(mode=0o700)
        self.listener = socket.socket(socket.AF_UNIX)
        endpoint = self.runtime / "home.sock"
        self.listener.bind(str(endpoint))
        endpoint.chmod(0o600)
        self.listener.listen()
        self.listener.settimeout(0.1)
        metadata = self.runtime / "runtime.json"
        metadata.write_text(json.dumps({
            "version": 1, "browser_pid": os.getpid(),
            "socket_path": str(endpoint), "nonce": "a" * 64,
        }))
        metadata.chmod(0o600)
        self.files = {"index.html": "base"}
        self.closed = False
        self.close_error = None
        self.reject_duplicate_open = False
        self.operation_result = None
        self.stop = threading.Event()
        self.workspaces = []
        self.methods = []
        self.server = threading.Thread(target=self.serve, daemon=True)
        self.server.start()

    def tearDown(self):
        self.stop.set()
        self.server.join(timeout=2)
        self.listener.close()
        for workspace in self.workspaces:
            shutil.rmtree(workspace, ignore_errors=True)
        shutil.rmtree(self.profile)

    def serve(self):
        while not self.stop.is_set():
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            with connection, connection.makefile("rb") as stream:
                for line in stream:
                    request = json.loads(line)
                    result = self.respond(request["method"], request["params"])
                    connection.sendall((json.dumps({
                        "version": 1, "id": request["id"], "result": result,
                    }) + "\n").encode())

    def respond(self, method, params):
        self.methods.append(method)
        if method == "hello":
            return {"ok": True}
        if method == "session.open":
            if self.reject_duplicate_open and self.methods.count(method) > 1:
                return {"ok": False, "error": {"code": "HOME_BUSY"}}
            self.closed = False
            return {"ok": True, "token": "test-session", "profile_id": "Default",
                    "expires_at_ms": 9999999999999.0}
        if self.closed:
            return {"ok": False, "error": {"code": "SESSION_REVOKED"}}
        if method == "session.close":
            if self.close_error:
                return {"ok": False, "error": {"code": self.close_error}}
            self.closed = True
            return {"ok": True}
        if method == "project.snapshot":
            return {"ok": True, "revision": "revision-1", "digest": digest(self.files),
                    "files": {key: base64.b64encode(value.encode()).decode()
                              for key, value in self.files.items()}}
        if method == "draft.stage":
            return {"ok": True, "draft_id": "draft-1", "digest": params["digest"],
                    "base_revision": params["base_revision"]}
        if method == "draft.preview":
            return {"ok": True, "publishable": True, "render": {"ok": True}, "sources": []}
        if method == "preview.screenshot":
            return {"ok": False, "error": {"code": "CAPTURE_FAILED"}}
        if method == "session.status" and "operation_id" in params:
            if self.operation_result is not None:
                return {"ok": True, "operation": self.operation_result}
            return {"ok": True, "operation": {"status": "not_found",
                                              "operation_id": params["operation_id"]}}
        return {"ok": True}

    def run_cli(self, *args):
        process = subprocess.run(
            [CLI, "home", *args, "--no-launch", f"--user-data-dir={self.profile}"],
            capture_output=True, text=True, timeout=10, check=False)
        self.assertTrue(process.stdout.strip(), process.stderr)
        return json.loads(process.stdout)

    def pull(self, *args):
        result = self.run_cli("pull", "--reason=test", *args)
        self.assertTrue(result["ok"], result)
        workspace = Path(result["workspace_dir"])
        self.workspaces.append(workspace)
        return workspace

    def read_state(self, workspace):
        return json.loads((workspace / ".dao-home/state.json").read_text())

    def test_clean_preserves_shared_session_until_explicitly_closed(self):
        first = self.pull()
        second = self.pull(f"--from={first}")
        before_clean = len(self.methods)
        result = self.run_cli("clean", f"--workspace={first}")
        self.assertTrue(result["ok"], result)
        self.assertFalse(first.exists())
        self.assertFalse(result["session_closed"])
        self.assertEqual(before_clean, len(self.methods))
        self.assertTrue(self.run_cli("status", f"--workspace={second}")["ok"])
        result = self.run_cli("clean", f"--workspace={second}", "--close-session")
        self.assertTrue(result["ok"], result)
        self.assertTrue(result["session_closed"])
        self.assertTrue(self.closed)

    def test_failed_explicit_session_close_preserves_workspace(self):
        workspace = self.pull()
        self.close_error = "LOCAL_IO_ERROR"
        result = self.run_cli("clean", f"--workspace={workspace}", "--close-session")
        self.assertFalse(result["ok"], result)
        self.assertEqual("LOCAL_IO_ERROR", result["error"]["code"])
        self.assertTrue(workspace.exists())
        self.assertFalse(self.closed)

    def test_failed_project_cleanup_preserves_metadata_for_retry(self):
        workspace = self.pull()
        original = self.read_state(workspace)
        generated = workspace / "project/generated"
        generated.mkdir()
        for index in range(2001):
            (generated / f"{index}.js").write_text("")
        result = self.run_cli("clean", f"--workspace={workspace}", "--discard")
        self.assertEqual("QUOTA_EXCEEDED", result["error"]["code"])
        self.assertEqual(original, self.read_state(workspace))
        self.assertTrue(self.run_cli("status", f"--workspace={workspace}")["ok"])
        (generated / "2000.js").unlink()
        self.assertTrue(self.run_cli("clean", f"--workspace={workspace}", "--discard")["ok"])
        self.assertFalse(workspace.exists())

    def test_resume_reuses_live_session_and_preview(self):
        self.reject_duplicate_open = True
        workspace = self.pull()
        self.assertTrue(self.run_cli("preview", f"--workspace={workspace}")["ok"])
        original = self.read_state(workspace)
        result = self.run_cli("resume", f"--workspace={workspace}", "--reason=Continue editing")
        self.assertTrue(result["ok"], result)
        self.assertEqual(original, self.read_state(workspace))
        self.assertEqual(1, self.methods.count("session.open"))

    def test_resume_recovers_pending_operation_in_live_session(self):
        self.reject_duplicate_open = True
        workspace = self.pull()
        state = self.read_state(workspace)
        state["pending_operation"] = {"kind": "publish", "operation_id": "lost-reply",
                                      "digest": digest(self.files)}
        (workspace / ".dao-home/state.json").write_text(json.dumps(state))
        result = self.run_cli("resume", f"--workspace={workspace}", "--reason=Recover publish")
        self.assertEqual("OUTCOME_UNKNOWN", result["error"]["code"])
        self.assertIn("pending_operation", self.read_state(workspace))
        self.operation_result = {"status": "committed", "operation_id": "lost-reply",
                                 "revision": "revision-2", "digest": digest(self.files)}
        result = self.run_cli("resume", f"--workspace={workspace}", "--reason=Recover publish")
        self.assertTrue(result["ok"], result)
        self.assertTrue(result["recovered"])
        self.assertEqual("revision-2", self.read_state(workspace)["base_revision"])
        self.assertNotIn("pending_operation", self.read_state(workspace))
        self.assertEqual(1, self.methods.count("session.open"))
        self.assertNotIn("draft.publish", self.methods)

    def test_unknown_outcome_keeps_original_and_allows_fresh_pull(self):
        first = self.pull()
        state = self.read_state(first)
        state["pending_operation"] = {"kind": "publish", "operation_id": "unknown",
                                      "digest": digest(self.files)}
        (first / ".dao-home/state.json").write_text(json.dumps(state))
        result = self.run_cli("status", f"--workspace={first}")
        self.assertEqual("OUTCOME_UNKNOWN", result["error"]["code"])
        self.assertIn("pending_operation", self.read_state(first))
        second = self.pull(f"--from={first}")
        self.assertTrue(first.exists())
        self.assertNotIn("pending_operation", self.read_state(second))
        self.assertEqual("base", (second / "project/index.html").read_text())
        self.assertNotIn("draft.publish", self.methods)

    def test_revoked_session_does_not_request_reapproval(self):
        workspace = self.pull()
        self.closed = True
        for args in (("pull", f"--from={workspace}"),
                     ("resume", f"--workspace={workspace}")):
            with self.subTest(command=args[0]):
                self.closed = True
                result = self.run_cli(*args, "--reason=test")
                if result.get("workspace_dir") and result.get("ok"):
                    self.workspaces.append(Path(result["workspace_dir"]))
                self.assertFalse(result["ok"], result)
                self.assertEqual("SESSION_REVOKED", result["error"]["code"])
        self.assertEqual(1, self.methods.count("session.open"))

    def test_screenshot_failure_preserves_successful_preview(self):
        workspace = self.pull()
        result = self.run_cli("preview", f"--workspace={workspace}", "--screenshot")
        self.assertTrue(result["ok"], result)
        self.assertTrue(result["publishable"])
        self.assertEqual({"ok": True}, result["render"])
        self.assertEqual("CAPTURE_FAILED", result["screenshot"]["error"]["code"])
        self.assertEqual([], result["artifacts"])

    def test_source_retest_invalidates_local_preview_receipt(self):
        workspace = self.pull()
        self.assertTrue(self.run_cli("preview", f"--workspace={workspace}")["ok"])
        self.assertTrue(self.run_cli("sources", "test", "source-1", f"--workspace={workspace}")["ok"])
        state = self.read_state(workspace)
        self.assertNotIn("preview_digest", state)
        self.assertNotIn("publishable", state)
        result = self.run_cli("publish", f"--workspace={workspace}", "--summary=test")
        self.assertEqual("PREVIEW_REQUIRED", result["error"]["code"])
        self.assertNotIn("draft.publish", self.methods)

    def test_build_assets_round_trip(self):
        self.files["dist/app.js"] = "application"
        self.files["build/styles.css"] = "styles"
        workspace = self.pull()
        result = self.run_cli("diff", f"--workspace={workspace}")
        self.assertTrue(result["ok"], result)
        self.assertEqual([], result["modified"])

    def test_pull_rejects_development_artifacts_before_checkout(self):
        self.files["node_modules/dependency.js"] = "development dependency"
        result = self.run_cli("pull", "--reason=test")
        if result.get("workspace_dir"):
            self.workspaces.append(Path(result["workspace_dir"]))
        self.assertFalse(result["ok"], result)
        self.assertEqual("WORKSPACE_INVALID", result["error"]["code"])
        self.assertTrue(self.closed)

    def test_staging_prunes_old_snapshots(self):
        workspace = self.pull()
        for index in range(4):
            (workspace / "project/index.html").write_text(str(index))
            self.assertTrue(self.run_cli("preview", f"--workspace={workspace}")["ok"])
        self.assertEqual(2, len(list((workspace / ".dao-home/base").glob("*.json"))))

    def test_oversized_summary_fails_before_creating_pending_operation(self):
        for command in (("publish",), ("rollback", "revision-1")):
            for summary in ("x" * 501, "界" * 167):
                with self.subTest(command=command, bytes=len(summary.encode())):
                    workspace = self.pull()
                    self.assertTrue(self.run_cli("preview", f"--workspace={workspace}")["ok"])
                    result = self.run_cli(*command, f"--workspace={workspace}", f"--summary={summary}")
                    self.assertFalse(result["ok"], result)
                    self.assertEqual("INVALID_ARGUMENT", result["error"]["code"])
                    self.assertEqual(500, result["max_summary_bytes"])
                    self.assertNotIn("pending_operation", self.read_state(workspace))


if __name__ == "__main__":
    unittest.main()
