---
name: dao-lite
description: Use when the user explicitly requests dao-lite, or when no Dao skill is specified and the task involves searches, research, or background browser automation, including signed-in sites and JavaScript-heavy pages. If the user specifies dao-browser, use that skill.
---

# Dao Lite

When the user explicitly requests `dao-browser` or `dao-lite`, use that skill.
Only when neither is specified, choose `dao-browser` for the user's existing
Dao tabs or windows and `dao-lite` for searches, research, and background web
tasks.

Before using Dao Lite in a task, run:

    dao-browser skill --lite

Follow the guide printed by the installed Dao. Read it again after Dao updates.
Keep the user's explicit skill choice when following the guide, even if an
older guide suggests another mode.
