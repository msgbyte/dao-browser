# Resource Usage Alert Validation

Date: 2026-11-20.

## Functional evidence

- `npm run rebuild -- -- -j 4 --target chrome dao_resource_usage_tests` passed.
- Eight native tests passed: sustained CPU, timestamp jitter/duplicates/gaps, memory sample separation, recovery/staleness, persistent feedback restoration, explicit dismissal, and painting on a transparent layer.
- Full WebUI suite: 1394 passed, 7 skipped, using the existing Python 3.12 installation and two workers. `npm run lint:lit` passed for 233 files.
- `node scripts/checks/resource-usage.mjs --functional` passed against a real isolated browser process. A busy worker raises the CPU marker; the foreground tab has no reminder; moving it into the background produces a reminder that survives another sample; manual dismissal survives the next sample while retaining the marker; terminating the worker eventually clears the marker.
- Actual rendering found a subpixel-text assertion in the new dismiss button. A native painting test reproduced it before the fix and passed afterward.

Memory thresholds and transient-feedback restoration are covered by native tests. Real-process functional testing exercises CPU load, not a multi-gigabyte allocation. Media/capture, split panes, navigation, and suspend/resume handling were inspected in code; this run does not claim an exhaustive interactive matrix for those conditions.

## Measurement method

Environment: macOS 26.5.2 (25F84), Mac16,10, 10 CPU cores, 16 GiB RAM. The `dao-debug` output is a component build. Existing applications remain running; the sampler only includes the isolated test browser and its descendants.

Command: `node scripts/checks/resource-usage.mjs --benchmark`.

- Same binary and local static pages in both conditions, with `--disable-features=DaoResourceUsageAlerts` for the disabled condition.
- 100 added tabs across 20 origins, plus normal browser internal pages; temporary profiles, no signed-in data, headless browser with the native sidebar initialized.
- Two pairs, with the second pair reversing order: off/on, then on/off.
- Each run warms up for 120 seconds, then samples for 180 seconds at 10-second intervals. No compilation, other test workload, or DevTools polling runs during the sample window.
- macOS `proc_pid_rusage` supplies cumulative CPU time, RSS, physical footprint, and wakeup counters. PID/start-time identities prevent accidental reuse. Process churn is recorded; CPU from an exited process after its last sample cannot be recovered.
- CPU percentages use one core as 100%. Tree RSS sums resident memory and may count shared mappings multiple times; physical footprint and browser-only counters provide additional context.
- Existing Chromium provider histograms are read before and after sampling to count queries and provider processing time. Memory-provider processing time excludes the asynchronous OS collection phase; process-tree CPU remains the primary overhead measure.

This is a same-build runtime comparison. It does not measure the patch's static code/resource size. Short samples on a shared machine cannot establish a universal worst-case bound or a statistically tight 5 MiB memory limit.

## Results

Raw readings: [resource-usage-samples.json](2026-11-20-resource-usage-samples.json).

All runs sampled 109 processes. One auxiliary process exited during the first interval of pair 1's enabled run; the other runs had no churn. Each measurement lasted approximately 180.03 seconds.

| Pair / order | Feature | Tree CPU (% of one core) | Main RSS (MiB) | Main footprint (MiB) | Tree footprint (MiB) |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 / first | Off | 0.3016 | 690.64 | 402.88 | 3581.56 |
| 1 / second | On | 0.3135 | 702.56 | 412.35 | 3592.69 |
| 2 / first | On | 0.3122 | 694.42 | 404.55 | 3563.80 |
| 2 / second | Off | 0.3241 | 697.97 | 392.30 | 3590.80 |

Differences below are enabled minus disabled within each pair. Memory values compare the median of each run.

| Metric | Pair 1 delta | Pair 2 delta | Mean delta |
| --- | ---: | ---: | ---: |
| Tree CPU (percentage points of one core) | +0.01186 | -0.01191 | -0.00003 |
| Main RSS (MiB) | +11.92 | -3.55 | +4.19 |
| Main footprint (MiB) | +9.46 | +12.25 | +10.86 |
| Tree RSS (MiB) | -170.77 | +331.51 | +80.37 |
| Tree footprint (MiB) | +11.13 | -26.99 | -7.93 |
| Idle wakeups / second | +4.44 | +0.48 | +2.46 |
| Interrupt wakeups / second | +4.32 | +1.87 | +3.10 |

Removing the interval containing the exited process gives a pair 1 CPU delta of +0.01365 percentage points and a two-pair mean delta of +0.00087. This does not change the CPU conclusion. A negative raw difference is measurement variability, not evidence that the feature saves CPU or memory.

Each enabled run recorded 7 CPU-provider queries versus 4 with the feature disabled. All four runs recorded 6 memory-provider queries. CPU-provider processing totaled 20.711/19.938 ms with the feature enabled and 9.155/11.229 ms disabled: approximately 10.1 ms extra provider wall time per three minutes. These counts confirm low-frequency shared collection rather than one query per tab. They include other Chromium clients' queries, not just Dao's fallback calls.

## Assessment

The CPU target of less than 0.1 percentage points of one core is supported for this 100-static-tab workload: paired differences stayed within approximately 0.014 percentage points even after excluding process churn. No stable CPU increase was resolved by these samples. There were no additional memory-provider queries in the measured windows.

The proposed strict 5 MiB memory target is **not validated**. Main RSS averaged +4.19 MiB but one pair exceeded 5 MiB; main physical footprint was consistently higher by 9.46–12.25 MiB. Whole-tree RSS varied substantially between fresh processes and shared mappings, while whole-tree footprint differences changed sign. Report the main-process increase and this variability rather than claiming a zero-memory-cost feature or selecting only the favorable metric. Further allocation profiling would be needed to isolate monitor-owned memory and establish a tighter bound.

The result covers a quiet, headless macOS component build with 100 static tabs. It is not a bound for all sites, release builds, platforms, long-running sessions, or continuous navigation.


## Settings follow-up

The default-on Settings > Performance control now gates Dao's additional monitor through the existing global performance-alert preference. This follow-up does not change the detection thresholds or sampling cadence; the A/B measurements above belong to the earlier implementation and were not repeated for the preference binding.

- App and dedicated native tests compile with `npm run rebuild -- -- -j 4 --target chrome dao_resource_usage_tests`.
- All 10 native tests pass, including disabling/resetting history and a real queued-query callback crossing a disable/re-enable cycle. Both added regressions failed before their corresponding fixes.
- `node scripts/checks/resource-usage.mjs --settings` passes: default-on control, immediate marker clearing, no recreation after the previous timer deadline, fresh history on re-enable, persisted disabled Local State, and a disabled control after restart.
- Settings screenshot inspected in Simplified Chinese; Lit lint passes for 233 files.
- Query observers are explicitly detached before destroying queries because in-flight requests can retain their old observer lists.

The broader background-window/system-notification behavior is a design proposal in [the controls design](2026-11-20-resource-alert-controls-design.md), not part of this settings follow-up.


## Background notification extension

- `npm run rebuild -- -- -j 4 --target chrome dao_resource_usage_tests` passed after the background behavior and dedicated notification handler changes.
- All 20 native tests passed. Coverage includes foreground suppression, inactive selected tabs, retained reminders across windows, profile isolation, aggregation, notification deduplication, explicit inspection/restoration, navigation and recovery cleanup, muted playback exclusion, hidden Agent Space exclusion, disable/reset handling, and the macOS native bridge accepting the notification type.
- The prior TRANSIENT notification path failed the native bridge regression. The dedicated DAO_RESOURCE_USAGE type now uses a handler registered by Chromium's notification display service, including after restart. Notification click tests use that handler rather than an in-process delegate.
- macOS headless windows can concurrently report key status. Cross-window tests deliver activation/deactivation events through the real Widget observer path. A separate interactive activation attempt could not acquire OS focus in this session and timed out; actual macOS banner display and OS permission behavior have not been manually confirmed.
- The fallback remains one 30-second timer and at most one Dao memory query per two minutes. Window changes use collection events and no additional polling timer. A ten-second app background grace period is evaluated on existing callbacks.
- The isolated load harness disables native notification presentation to avoid posting test-load alerts to the user's notification center. Native routing is checked separately by the browser tests.

- The actual Settings page passed default-on UI, immediate clearing, continued suppression beyond the former timer deadline, fresh re-enable, saved preference, and restart persistence. The updated Chinese label/description screenshot was inspected.


### Follow-up 100-tab sample

Raw readings: [resource-background-samples.json](2026-11-20-resource-background-samples.json).

One additional off/on pair used the final component build, 100 static tabs over
20 local origins, 120 seconds of warm-up and 120 seconds of measurement per run.
Both runs tracked 109 processes without process churn. Native notification
presentation was disabled in both isolated profiles; CPU/memory monitoring was
controlled independently with DaoResourceUsageAlerts. No compilation, other
load tests or DevTools polling ran during measurement.

| Metric | Off | On | On minus off |
| --- | ---: | ---: | ---: |
| Tree CPU (% of one core) | 0.341485 | 0.348833 | +0.007348 percentage points |
| Browser CPU (% of one core) | 0.002761 | 0.004727 | +0.001966 percentage points |
| Browser RSS (MiB) | 694.72 | 660.61 | -34.11 |
| Browser physical footprint (MiB) | 403.74 | 350.67 | -53.06 |
| Tree physical footprint (MiB) | 3582.67 | 3537.88 | -44.79 |
| Idle wakeups / second | 55.27 | 64.72 | +9.45 |
| Interrupt wakeups / second | 112.48 | 133.78 | +21.30 |
| CPU-provider queries | 3 | 5 | +2 |
| Memory-provider queries | 5 | 5 | 0 |

CPU-provider processing was 8.068 ms off and 17.401 ms on, an additional
9.333 ms over two minutes. This pair supports the absence of a large idle CPU
regression for the new background policy and confirms unchanged low-frequency
query behavior. It is a single ordered pair, not a statistical guarantee.

The lower enabled-run memory readings are run-to-run variability, not evidence
that monitoring saves memory. The strict 5 MiB incremental memory target remains
unvalidated. Wakeup rates rose in this pair, and neither battery drain nor energy
was measured; no power-overhead guarantee is made. Long-running active workloads,
continuous navigation and real OS notification presentation remain outside this
sample.


## Review fixes — 2026-11-22

Three new browser regressions failed before the fixes: an excluded high-CPU
page prevented a second ordinary notification incident; Chromium Copy Link
covered the still-visible persistent reminder; and a 100-character title
produced a 1462 DIP toast in a 518 DIP available area.

Notification deduplication now follows pages that actually participated in a
reminder, while preserving deduplication through temporary media suppression.
Chromium toast creation and destruction suspend and restore the retained
reminder, including queued replacements and dismissal during transient feedback.
Normal and Mini Dao layouts constrain toast width so the label elides and both
controls remain visible. Mini Dao also reserves room for the entry animation.

Verification on the final sources:

- `npm run rebuild -- -- -j 4 --target chrome dao_resource_usage_tests` passed.
- All 23 focused browser, notification, state, and toast tests passed with
  `--headless --use-mock-keychain --test-launcher-jobs=1 --test-launcher-retry-limit=0`.
- The new regressions cover notification re-arming with an excluded page still
  busy, temporary media suppression without duplicate notification, Chromium
  toast queue replacement, dismissal without resurrection, and long labels with
  fully visible controls while resizing both window types.
- `git diff --check` passed.

The fixes add no sampling queries or timers. The 100-tab A/B benchmark and actual
macOS notification presentation were not repeated. The earlier memory and power
measurement limits remain unchanged.
