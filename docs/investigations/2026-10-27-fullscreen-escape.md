# macOS delayed fullscreen exit after a short Escape press

## Environment and evidence limits

The supplied incident records Dao 1.0.124 on macOS 15.7.4: a native Escape
release after 115.990 ms, no exclusive-access release/cancellation record, and
a held-Escape fullscreen exit approximately 1500 ms after key down.
The original evidence directory is absent on this machine, so the supplied
incident timeline has not been independently checked against its raw log.
The user confirmed the reproducible sequence is browser fullscreen, Cmd+T,
then a short Escape press.

This investigation uses checkout `6445f3b3` (Dao 1.0.125), Chromium
`155.0.8059.86` at `804d6b05f089b27af8342622a36558c49401a1d4`, and
macOS 26.5.2. Unlike the handoff environment, this checkout has Chromium sources
and a usable browser-test build. `/Applications/Dao.app` is an unrelated old
1.0.29 installation; it is not evidence for the current source behavior.

## Event routing

1. Cmd+T or Cmd+L opens `DaoCommandBarView` and focuses its native textfield.
2. The native Views accelerator route calls
   `BrowserView::AcceleratorPressed()` and then
   `ExclusiveAccessManager::HandleUserKeyEvent()`. Eligible Escape down starts
   the 1500 ms exit timer and 500 ms hint timer.
3. `DaoCommandBarView::HandleKeyEvent()` handles Escape down. `CancelNewTab()`
   or `Hide()` removes the bar and requests focus on the content WebView.
4. Escape up now targets `RenderWidgetHostViewCocoa`. In
   `keyEvent:wasKeyEquivalent:`, its `_unmatchedKeyDownCodes` set contains no
   Escape down because that event belonged to the native textfield. The
   `numErased < 1` guard returns before `ForwardKeyboardEvent()`.
5. Consequently, neither `BrowserWebContentsDelegate::PreHandleKeyboardEvent()`
   nor the native release accelerator reaches the manager. Its exit timer can
   fire after the user has already released Escape.

The unmatched-release filter intentionally protects webpages from key releases
that belong to other native controls or windows. Removing it or forwarding an
unmatched release to the renderer would change unrelated input semantics.
The existing `RenderWidgetHostImpl::suppress_events_until_keydown_` Escape
exception is downstream of this filter and cannot recover this release.

## Fix boundary

`BrowserCrApplication::sendEvent` observes Escape release before native
responder routing. It resolves the manager from the event's own native window
and calls `ExclusiveAccessManager::CancelPendingEscapeHold()`. It does not
consume, modify, or reinject the event. The shared method stops both timers and
notifies controllers once, preserving the weak-pointer lifetime checks. A later
ordinary release delivery is a no-op for this cleanup.

The application entry point also covers Command-modified releases. Chromium
forwards these directly to the key window instead of calling the superclass;
that branch bypasses ordinary AppKit local event monitors. A regression test
exposed this gap in an earlier window-monitor implementation, which was replaced
with the shared application-dispatch hook.

Page input also has an asynchronous return path: an unhandled renderer event can
reach the native accelerator again, and `CommandDispatcher` can redispatch it
through `NSApplication`. Rapid taps exposed an old release canceling a newer
hold; a delayed key-down reply could otherwise rearm a canceled timer. On macOS,
the manager records event timestamps and ignores older events for hold-timer
updates. Native release and deactivation advance that boundary while canceling a
pending hold. Normal event delivery and the other controllers still run. This
ordering guard is not enabled on other platforms.

`BrowserNativeWidgetMac::OnWidgetActivationChanged` cancels the window's pending
hold on deactivation, so a release delivered to another window cannot leave an
old action armed. Releases from unrelated windows do not cancel its timer.
No observer registration or lifetime is added. The shared cancellation is a
no-op without a running timer; feature gating, timer duration, key-down
eligibility, fullscreen exits, and lock controllers are unchanged.

No command-bar focus workaround, synthetic release, new setting, or extra delay
is introduced. Existing temporary fullscreen diagnostics remain available for
comparison with the incident environment. Their removal instructions distinguish
the diagnostic app-local monitor from the functional native dispatch hook.

## Verification

The native regression fixture is
[`DaoFullscreenEscapeMacBrowserTest`](../../src/dao/browser/ui/views/dao_fullscreen_escape_mac_browsertest.mm).
It sends `NSEvent` objects through `NSApplication::sendEvent`, including Cmd+T/L,
and does not send Escape directly to the exclusive-access manager. The
command-bar cases assert that page focus has moved to `RenderWidgetHostViewCocoa`
and that its unmatched-release filter still suppresses Escape up.

The fixture covers ten Cmd+T and ten Cmd+L dismissals with 100 ms key holds and
three-second observation windows; page taps; a release with Command held;
long-press auto-repeat; a 300 ms page key handler that delays the renderer reply;
native/page focus changes; window isolation and
deactivation; and page fullscreen, pointer lock, and keyboard lock. It checks
controller cleanup counts as well as timer, focus, tab, navigation, and
fullscreen state.

Native event routing is exercised with Chromium's simulated macOS fullscreen
transitions. The deactivation fixture uses Chromium's `MockActivationController`
because Dao's browser-test harness deliberately prevents foreground activation.
Keyboard-lock coverage uses the upstream fixture's mock permissions
and disables the OS keyboard hook. Real AppKit fullscreen animation, physical
input, and macOS 15 remain separate acceptance checks.

### Failing baseline

Built the original runtime with the new tests using:

```sh
npm run rebuild -- -- --target chrome browser_tests -j 6
engine/src/out/dao-debug/browser_tests \
  --gtest_filter='DaoFullscreenEscapeMacBrowserTest.*DismissalCancelsHold' \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0
```

Both cases failed as expected: the timer remained armed after key-up, the
held-Escape callback ran, and browser fullscreen became false. The native page
responder and unmatched-key-up assertions passed. Separate test processes avoid
Chromium's prohibition on running multiple browser tests in one process.

For Cmd+T, PID 99056 and window 12215 produced this timeline on 2026-10-27
(Asia/Shanghai):

| Time | Event |
| --- | --- |
| 22:23:01.395 | Native Escape down, timestamp `621459.216039` |
| 22:23:01.396 | Manager Escape down and `escape-hold-start` |
| 22:23:01.512 | Native Escape up, timestamp `621459.333374` |
| 22:23:02.896 | `escape-hold-fire`, `exit-source-held-escape`, `request-exit` |

The native hold lasted 117.335 ms. There was no manager release or hold-stop
record between release and timer expiry. Cmd+L independently reproduced the
same sequence in PID 99122, window 12218, with a 117.920 ms native hold.
This matches the supplied incident's missing-release signature while identifying
the specific responder filter responsible.

The build also exposed four existing test compile errors, fixed without product
changes: a `raw_ptr` child loop, two capturing `BindOnce` lambdas, and chaining
`DictValue::Set()` on an lvalue. The corresponding four tests are included in the
final focused validation.

### Existing upstream test mismatch

`ExclusiveAccessManagerPressAndHoldEscTest.ShowExclusiveAccessBubble` expects
Chromium's browser-fullscreen hint to be visible on entry and after 500 ms.
Dao already intentionally suppresses this hint for sidebar browser windows in
`browser_view.cc.patch`, while retaining page-fullscreen and lock warnings.
That upstream test fails for this existing product difference. It is excluded
from the final targeted filter and replaced with Dao's existing
`DaoSidebarFullscreenBrowserTest.BrowserFullscreenDoesNotShowExitBubble`.
The failing upstream expectation was not changed or disabled in the repository.

### Fixed runtime evidence

In the final build, the same native route still drops the unmatched page
release, while the application hook cancels the pending hold first. Cmd+T ran
in PID 45109, window 12364; Cmd+L ran in PID 45411, window 12367.

| Final-build observation | Cmd+T | Cmd+L |
| --- | --- | --- |
| Native Escape down/up pairs | 10 | 10 |
| Hold starts / early cancellations | 10 / 10 | 10 / 10 |
| Hold fires / fullscreen exit requests | 0 / 0 | 0 / 0 |
| Per-press observation after release | 3 seconds | 3 seconds |

For the first Cmd+T repetition, native down was at `22:50:13.964`
(timestamp `623091.762028`), hold start at `22:50:13.967`, and early cancellation
at `22:50:14.081`, the same wall-clock millisecond as native up
(timestamp `623091.879275`). The native hold was 117.247 ms. The first Cmd+L
hold was 118.728 ms with the same cancellation pattern. These logs cover the
complete ten-repetition processes, including the wait after the final release.
No `escape-hold-fire`, `exit-source-held-escape`, or `request-exit` was recorded
in either process.

The application hook runs before the diagnostic local monitor, so a hold-stop
record can precede the native-key-up record. All records still report
`native_fullscreen=0` and `widget_fullscreen=1`: these are simulated fullscreen
transitions, not evidence of real Space animation or physical keyboard input.

The delayed-page case independently confirmed the ordering guard at runtime:
in PID 45774, window 12373, Escape down started the hold at `22:51:31.669`;
native release canceled it at `22:51:31.781`; the old key-down reply reached the
manager at `22:51:31.971`. It did not produce another hold-start record, and the
window remained fullscreen through the three-second observation. These records
are in `/tmp/dao-fullscreen-final-delayed-page.log`.

### Final validation commands and results

Compilation completed successfully with:

```sh
npm run rebuild -- -- --target chrome browser_tests -j 6
```

The following focused run passed all 33 tests with retries disabled in
197 seconds. Ten are the new native Escape tests; the remainder cover existing
exclusive-access behavior, Dao's fullscreen hint policy, and the four repaired
test compilation sites.

```sh
engine/src/out/dao-debug/browser_tests \
  --gtest_filter='DaoFullscreenEscapeMac*Test.*:DaoSidebarFullscreenBrowserTest.BrowserFullscreenDoesNotShowExitBubble:ExclusiveAccessManagerTest.*:ExclusiveAccessManagerPressAndHoldEscTest.*:FullscreenControllerPressAndHoldEscTest.*:FullscreenControllerPressAndHoldEscKillSwitchTest.*:FullscreenControllerTest.RepeatedEscEvents*:FullscreenControllerTest.KeyboardLockExitsOnEscPressWhenEscNotLocked:FullscreenControllerTest.KeyboardLockDoesNotExitOnEscPressWhenEscIsLocked:DaoCommandBarBrowserTest.RemoveSuggestionPreservesTextEditing:DaoAgentCursorViewBrowserTest.HideCompletesActiveMoveExactlyOnce:DaoAgentCursorViewBrowserTest.ShowAtCenterCancelsMoveBeforeReentrantMove:DaoMcpPageToolsBrowserTest.GuardedClickRevalidatesAfterCursorMove-ExclusiveAccessManagerPressAndHoldEscTest.ShowExclusiveAccessBubble' \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0 \
  --test-launcher-summary-output=/tmp/dao-fullscreen-final-tests.json
```

`npm run docs:check` and `git diff --check` also passed. Each of the four runtime
patches applied to the pristine Chromium revision without fuzz or offsets, and
the resulting files matched the imported build inputs byte for byte. The
superseded event-monitor header changes were removed through the import pipeline.

Local evidence artifacts from this session:

- `/tmp/dao-fullscreen-before-isolated.log` and
  `/tmp/dao-fullscreen-before-native.log`: failing baseline and native records.
- `/tmp/dao-fullscreen-final-build.log`: successful final compilation.
- `/tmp/dao-fullscreen-final-tests.log` and
  `/tmp/dao-fullscreen-final-tests.json`: 33 successful test results.
- `/tmp/dao-fullscreen-final-native.log`: final Cmd+T/L native records.

No release was packaged or installed. Remaining manual acceptance is physical
input and real fullscreen/Space transitions on macOS 15.7.4 and macOS 26,
including switching to another application while Escape is held. The native
automated checks above establish event routing and controller behavior in the
test environment; they do not substitute for those OS/window-server checks.

## Follow-up verification: 2026-10-27, 23:43–23:48 Asia/Shanghai

The user requested another end-to-end check. No implementation or test source
changed during this run; SHA-256 snapshots of all changed files confirmed that
the tested inputs stayed identical throughout verification.

- Rebuilt with `npm run rebuild -- -- --target chrome browser_tests -j 6`:
  successful, 33 build steps, 39.67 seconds.
- Reapplied the four runtime patches to pristine Chromium in temporary
  directories, without fuzz or offsets, and compared their results byte for
  byte with the imported build inputs. The native test source also matched.
- Repeated the full 33-test filter above, with retries disabled and summary
  output changed to `/tmp/dao-fullscreen-reverify-tests.json`: 33 successes,
  zero failures, exit code 0, 199 seconds.
- `npm run docs:check` and `git diff --check` passed.

| Native event evidence | Cmd+T | Cmd+L |
| --- | --- | --- |
| PID / window | 86417 / 12493 | 86687 / 12496 |
| Repetitions, each followed by 3 seconds | 10 | 10 |
| Measured native hold duration | 117.281–119.248 ms | 116.837–119.785 ms |
| Hold starts / early cancellations | 10 / 10 | 10 / 10 |
| Hold fires / fullscreen exit requests | 0 / 0 | 0 / 0 |

The run also passed consecutive page taps, Command-modified release, delayed
renderer reply, held Escape with repeat, both focus directions, window isolation,
deactivation, page fullscreen, pointer lock, keyboard lock, and the feature's
kill-switch behavior. The existing upstream hint-visibility mismatch remains
excluded for the reason documented above; this was a targeted run, not the full
browser-test suite.

### Real desktop verification remains blocked

A fresh current-build application was launched through `npm run start:debug`
with an isolated profile at `/tmp/dao-fullscreen-e2e-20261027-reverify`,
`--no-first-run --no-default-browser-check about:blank`. Its PID was 86133;
native diagnostics confirmed window creation. It uses macOS 26.5.2 (25F84).
The running application was retained for follow-up desktop verification.

The desktop tool repeatedly returned `cgWindowNotFound` when selecting the
exact built application path. Bundle-ID selection was ambiguous because another
local Dao Debug application shares the identifier, and the macOS tool does not
support selecting a native window by numeric ID. The user was asked to show the
test window on the current desktop; no answer was available when the automated
run finished. No real fullscreen keyboard sequence was performed. Actual
fullscreen/Space transitions, physical keyboard input, and macOS 15.7.4 remain
unverified; the successful native tests still use simulated fullscreen.

Follow-up artifacts:

- `/tmp/dao-fullscreen-reverify-build.log`
- `/tmp/dao-fullscreen-reverify-tests.log` and `.json`
- `/tmp/dao-fullscreen-reverify-native.log`
- `/tmp/dao-fullscreen-reverify-evidence.json`
- `/tmp/dao-fullscreen-reverify-source.json`
- `/tmp/dao-fullscreen-e2e-launch.log` and
  `/tmp/dao-fullscreen-e2e-runtime.log`
