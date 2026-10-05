# WebView2 Installer Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans. Work in the primary checkout; do not commit or create branches.

**Goal:** Give Dao's Windows installer a local HTML/CSS interface hosted by Win32/WebView2, retaining the native NSIS fallback and Chromium installation backend.

**Architecture:** NSIS starts the bundled host before showing its native pages. The host opens bundled content with NavigateToString and sends installation requests to another silent invocation of the same NSIS EXE. That invocation skips the host and performs existing directory validation and Chromium installation. Host initialization failure returns 77 to show the original native wizard; cancellation or completion exits the outer wrapper without installing again.

**Tech Stack:** Win32 C++, WebView2 SDK/WRL, local HTML/CSS/JavaScript, NSIS, existing TypeScript CLI and Vitest.

**Spec:** The user-approved conversation design: WebView2 primary UI; Win32 owns folder selection and installation; existing mini_installer owns browser installation; NSIS remains available offline when WebView2 is unavailable. No runtime downloads on end-user machines.

## Global Constraints

- English and Simplified Chinese strings belong in locale resources.
- Keep the existing per-user path and repair rules, silent mode, release version and artifact naming.
- No direct engine edits. Compile only through `npm run rebuild`; add a standalone `dao_installer_ui` target without touching the Chromium cache.
- Load only bundled content, validate message origins and commands, disable external navigation/permissions, and never expose arbitrary command execution.
- Do not close the host while an installation is active; keep the backend alive until its result is known.

## Interfaces

- Frontend: `mountInstaller(bridge, locales)` exported from `web/app.js`. Bridge exposes `postMessage(string)` and `addEventListener('message', handler)`.
- Web to host: `ready`, `browse:<directory>`, `install:<directory>`, `minimize`, `cancel`, `finish`, `launch`. Other messages are ignored.
- Host to web: `{type:'init', language:'en'|'zh-CN', directory, locked, version, logPath}`; `{type:'directory', directory}`; `{type:'state', state:'installing'|'complete'|'error', code?, error?:'invalidDirectory'|'installFailed'|'launchFailed'}`.
- HTML template tokens: `{{STYLE}}`, `{{SCRIPT}}`, `{{LOCALES}}`, `{{ICON_DATA_URL}}`. The packager embeds assets, emits local `index.html`, and verifies the host source fingerprint before publishing the EXE.
- Host reads `session.ini` beside its EXE, section `Installer`, with `Executable`, `Directory`, `Language` (1033/2052), `Version`, and `Locked` (0/1). This avoids inserting user-controlled paths into host arguments. Exit 77 means safe fallback before installation, 0 success, 1 cancellation, other codes failure. The outer EXE supports `/NATIVE` to select the fallback explicitly.

## Review Focus

- Runtime absent, initialization failure or timeout: return to NSIS without a duplicate installation.
- Invalid paths, spaces, Unicode and existing installations: preserve the current backend's validation and upgrade lock.
- Untrusted navigation/messages: no external content or generic native command bridge.
- Duplicate clicks and window close during installation: one backend process, completion or error reflected accurately.
- Fonts, localization, high contrast and DPI: readable, keyboard-operable UI with a square logo.

## Tasks

- [x] Implement the local frontend and focused state/bridge/i18n tests. Test install success, failures, repair locking, repeated clicks and escaped path text with a mock bridge.
- [x] Implement the Win32 host, local content isolation, runtime fallback, folder picker and asynchronous silent-installer lifecycle. Build through `npm.cmd run rebuild -- -- --release --target dao_installer_ui`.
- [x] Integrate NSIS dispatch and packaging with pinned SDK/build metadata. Extend packaging tests for host assets, stale builds and failed packaging without artifact replacement.
- [x] Validate the real host with an isolated backend, exercise runtime fallback and native silent paths, inspect screenshots, update feature inventory/development/checklist, and package the release EXE with its SHA-256 sidecar.

## Execution Record

- Design approved in conversation. User authorization supersedes redundant skill approval gates.
- Existing native installer work and uncommitted changes are preserved.
- The standalone host compiled successfully through the approved rebuild command; no Chromium compilation was needed.
- The five focused Vitest files passed all 28 tests. TypeScript and documentation checks also passed.
- The real WebView2 host passed the isolated backend suite in English and Simplified Chinese, including missing-runtime exit 77, blocked external navigation, Unicode paths, locked repair paths, failure/retry, duplicate submission and launch failure. Visible captures verified the logo and rendered pages.
- The native NSIS fixture suite passed with isolated installation files and registration. It exercises directory validation, repair locking and silent backend results without installing Dao.
- Packaging produced the 1.0.108 Windows x64 EXE and SHA-256 sidecar. The actual packaged EXE opened the localized repair page, was captured and then cancelled with exit code 1, without invoking installation.
- Full installation/uninstallation with the real Chromium payload and runtime failure during an active installation remain manual release checks on a disposable Windows machine. The automated host and NSIS suites exercise those components separately with harmless backends.
- A subsequent user-supplied design replaces the initial UI with the compact 560 x 420 frameless window, transparent Dao logo, system light/dark themes, inline path editor and explicit launch button. Installation policy remains per-user, with indeterminate progress and no unsafe cancellation or simulated progress.
