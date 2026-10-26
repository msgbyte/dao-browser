#!/usr/bin/env python3
"""Exercise temporary macOS fullscreen diagnostics and verify unified logs."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time


CASES = {
    "MacFullscreenControllerTest.SimpleUserInitiated": {
        "cocoa-will-exit", "cocoa-did-exit", "state-before", "state-after",
    },
    "MacFullscreenControllerTest.SimpleProgrammatic": {
        "native-request-exit", "native-toggle", "cocoa-did-exit",
    },
    "MacFullscreenControllerTest.FailEnterFullscreenSimple": {
        "native-enter-rolled-back", "cocoa-did-exit",
    },
    "MacFullscreenControllerTest.FailToEnterFullscreenViaDelegate": {
        "cocoa-enter-failed",
    },
    "MacFullscreenControllerTest.FailExitFullscreen": {
        "native-exit-rolled-back", "cocoa-did-enter",
    },
    "MacFullscreenControllerTest.CrossScreenFromFullscreen": {
        "native-cross-display-exit", "native-move-display", "native-toggle",
    },
    "DaoFullscreenDiagnosticsBrowserTest.NativeFailureCallbacksKeepWindowedState": {
        "cocoa-enter-failed-callback", "cocoa-exit-failed-callback",
        "native-window-bound", "native-window-will-close",
    },
    "DaoSidebarFullscreenBrowserTest.BrowserFullscreenDoesNotShowExitBubble": {
        "request-exit", "native-request-exit", "native-exit-start",
        "native-exit-complete", "native-transition-exit-start",
        "native-transition-exit-complete",
    },
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("macOS unified logging is required")
    binary = args.binary.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="dao-fullscreen-log-test-") as temp:
        for name, expected in CASES.items():
            output = Path(temp, "test.log")
            with output.open("w") as stream:
                process = subprocess.Popen(
                    [str(binary), "--single-process-tests", f"--gtest_filter={name}"],
                    stdout=stream, stderr=subprocess.STDOUT,
                )
                try:
                    result = process.wait(timeout=120)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    raise
            if result or "[  PASSED  ] 1 test." not in output.read_text():
                raise AssertionError(f"{name} did not pass:\n{output.read_text()}")
            predicate = (
                'subsystem == "com.dao.browser" AND category == "fullscreen" '
                f"AND processIdentifier == {process.pid}"
            )
            deadline = time.monotonic() + 15
            while True:
                query = subprocess.run(
                    ["/usr/bin/log", "show", "--last", "3m", "--style", "ndjson",
                     "--predicate", predicate],
                    check=True, capture_output=True, text=True,
                )
                messages = [json.loads(line).get("eventMessage", "")
                            for line in query.stdout.splitlines() if line.startswith("{")]
                records = [dict(part.split("=", 1) for part in message.split()
                                if "=" in part) for message in messages]
                found = {record.get("event") for record in records
                         if record.get("debug_only") == "1"}
                missing = expected - found
                if not missing or time.monotonic() >= deadline:
                    break
                time.sleep(1)
            if missing:
                raise AssertionError(f"{name}: missing log events {sorted(missing)}")
            native = [record for record in records
                      if record.get("layer") == "native" and "event" in record]
            assert native and all("controller" in record and "state" in record
                                  and "id" in record for record in native), name
            print(f"PASS {name}: {len(records)} log records", flush=True)


if __name__ == "__main__":
    main()
