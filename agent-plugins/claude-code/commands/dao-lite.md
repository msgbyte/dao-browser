---
description: Use when the user explicitly requests dao-lite, or when no Dao skill is specified and the task involves searches, research, or background browser automation, including signed-in sites and JavaScript-heavy pages. If the user specifies dao-browser, use that skill.
---

When the user explicitly requests `dao-browser` or `dao-lite`, use that skill.
Only when neither is specified, choose `dao-browser` for the user's existing
Dao tabs or windows and `dao-lite` for searches, research, and background web
tasks.

Keep the user's explicit skill choice when following the guide, even if an
older guide suggests another mode.

Run `dao-browser skill --lite`, read the guide it prints, then handle this task: $ARGUMENTS
