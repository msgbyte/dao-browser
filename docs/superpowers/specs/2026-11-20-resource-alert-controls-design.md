# Resource Alert Controls and Background Behavior

The user requests a Settings switch, enabled by default, and a design centered on sustained resource usage while Dao or an individual tab is in the background.

## Authorized settings change

Reuse the existing global switch in Settings > Performance, backed by `performance_tuning.intervention_notification.enabled`, whose registered default is already true. Label it Background resource alerts and explain that turning it off stops this feature's checks. Existing user choices remain respected.

Turning it off immediately stops Dao's fallback timer, releases both query subscriptions and per-page state, and clears indicators and retained reminders across windows. Already queued results must not recreate alerts. Turning it on starts a fresh observation period without restoring old warnings. Chromium's own resource services remain independent.

Approaches considered: reusing this existing performance-alert preference avoids duplicate controls; adding a second Dao preference would create two similarly named settings; hiding notifications without stopping checks would not satisfy the low-overhead intent. Reuse is the implementation choice.

## Approved background behavior

- A tab visible in the active Dao window, including a split pane, only shows a static indicator.
- A sustained heavy tab in the background, including the selected tab of an inactive/minimized Dao window, becomes eligible after a one-minute background grace period.
- When another Dao window is active, present a persistent summary in the active window for the same profile. Group multiple affected tabs into one reminder with a count and a View usage action that opens the existing browser Task Manager; a single-tab reminder can retain its View tab action. Detection thresholds remain unchanged.
- When all Dao windows are in the background, send one system notification for the current incident and retain the in-browser reminder. The user explicitly approved this behavior.
- A ten-second app background grace period avoids notifications during normal window switching. Existing sample callbacks drive delivery; there is no additional timer.
- Notification arrival never activates Dao or changes tabs. Only an explicit notification/action click brings Dao forward. System notification copy uses a generic count rather than exposing page titles.
- A reminder created in the background remains available when returning to Dao until inspected, explicitly dismissed, or all affected tabs recover. Returning to the app does not itself create another notification. Normal foreground work does not initiate an intrusive alert.
- Media playback, capture, and explicit High Performance Mode retain their existing exclusions. No automatic tab termination, reload, or discard.
- Notification delivery should reuse existing Chromium/Dao notification infrastructure and activation events, with one application-wide coordinator and no extra polling loop. System notification presentation follows OS settings; native in-browser reminders use DaoToastView.

The default-on switch is implemented and validated. The approved background behavior extends the same monitor and switch.
