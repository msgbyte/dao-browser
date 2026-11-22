# Sustained Resource Usage Alerts

The user approved low-frequency sustained CPU/memory detection, with a persistent, manually dismissible reminder replacing the originally proposed transient toast. Foreground tabs only show a static sidebar marker. Background tabs show the marker and a persistent notification using DaoToastView. Split panes count as foreground; media/capture and High Performance Mode suppress the notification. No automatic reload, close, freeze, or discard.

CPU is evaluated at 30-second intervals: at least 80% of one core for 90 seconds raises an alert; below 40% for 60 seconds clears it. Memory uses fresh measurements at least 120 seconds apart: two readings at or above 2 GiB (1 GiB on machines with at most 8 GiB RAM) raise an alert; two readings below 80% of the threshold clear it. Missing/stale measurements, navigation and sleep must not fabricate sustained usage.

One browser-wide monitor passively shares Chromium Resource Attribution results and issues bounded fallback queries only when fresh samples are unavailable. Memory is never queried once per tab. No page scripts, history database, continuous animation, or per-tab timers. Closing a reminder suppresses that episode; the sidebar marker persists until recovery. Normal transient toasts temporarily cover the retained reminder and restore it afterward.

The proposed incremental budget is below 0.1% of one CPU core and 5 MiB resident memory at 100 tabs. These are validation targets, not measured claims. Use isolated profiles and real browser process-tree A/B sampling, report noise and limits, and avoid claiming a guarantee from short measurements.

Approach: shared low-frequency sampling balances coverage and cost. Passive-only monitoring cannot guarantee fresh data; frequent independent polling adds unnecessary work. Existing native notification appearance and internationalization remain authoritative.

The subsequently approved [background controls design](2026-11-20-resource-alert-controls-design.md) extends foreground/background handling to window activation, groups reminders by profile, and adds one system notification per incident while retaining the in-browser reminder.
