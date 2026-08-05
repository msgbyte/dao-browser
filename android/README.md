# Dao Browser Android

Dao Browser Android is a Kotlin application built with Mozilla Android Components and GeckoView.

## Requirements

- macOS on the existing development Mac
- JDK 17
- Android SDK 36
- An Android API 26+ emulator or physical device

Set the local SDK location before running Gradle, for example:

```bash
export ANDROID_HOME="$HOME/Library/Android/sdk"
```

All commands run locally on the development Mac; they do not require a cloud build service or paid device hardware.

## Verify

```bash
cd android
./gradlew :app:verifyBundledUBlockOrigin
./gradlew :app:verifyBundledKissTranslator
./gradlew :app:testDebugUnitTest :app:lintDebug :app:assembleDebug
```

The APK bundles a GeckoView-compatible uBlock Origin 1.72.2 package and the
unmodified KISS Translator 2.0.29 Firefox package. Both are
requested as built-in extensions before creating the first browser session and
update with the APK without a first-run download. KISS Translator supplies
paragraph-by-paragraph bilingual page translation, input and selected-text
translation, and subtitle translation through the service selected inside the
extension.

The Extensions screen reads installed extensions from Gecko and can toggle
them. Its Store is native Compose UI: a blank query loads AMO recommendations,
submitted queries search AMO, and catalog requests are limited to Android
extensions compatible with the running Gecko major version. The catalog accepts
only public HTTPS `.xpi` packages and installs the selected AMO package directly
through Gecko's `RTAMO` path. Opening, searching, or installing from the Store
does not change the selected browser tab.

Before either Store or local installation completes, Dao presents Gecko's
requested browser permissions, site access, and data-collection permissions for
explicit approval; cancellation declines the install. The system document picker
remains available for local `.xpi` files without a storage permission. That
package is staged for Gecko's `FROM_FILE` path until the installation reaches a
terminal result. User-installed extensions persist across restarts and can be
disabled or uninstalled. Release GeckoView enforces Mozilla signatures,
compatibility, and blocklists for both install paths, so Dao does not bypass
unsigned, incompatible, corrupt, or blocklisted-package rejection. Catalog
failures show a Retry action. Remote installation failures use the localized
extension-install status; after the user dismisses the result with Done, the
add-on's Install action is available again. A dedicated Dao ad-blocking
preference is not implemented.

## Localization

Android uses English as its unqualified source catalog and follows the system
locale through standard Android resource resolution. Simplified Chinese is
hand-authored under `values-zh-rCN`; unsupported locales fall back to English.
Desktop and Android localization files remain independent.

From the repository root, preview translation work without making API calls or
writing files:

```bash
sh ./i18n.sh --dry-run
sh ./i18n.sh --only android --langs zh-CN,ja --dry-run
```

Run the desktop and Android translators with OpenAI GPT-5.5:

```bash
OPENAI_API_KEY=sk-... sh ./i18n.sh
```

Use `--only desktop` or `--only android` to run one platform, and use
`OPENAI_TRANSLATE_MODEL` or `--model` to override the default model. A live run
uses the OpenAI API and may incur charges; build and test commands never invoke
it automatically.

## Install

```bash
cd android
./gradlew :app:installDebug
```

## Device smoke test

```bash
cd android
./gradlew :app:connectedDebugAndroidTest
```

The application uses Mozilla Android Components `BrowserStore` and
`EngineMiddleware` for multiple Gecko tabs. Its full-screen tab grid supports
live tab counts, switching, close and swipe-right-to-close, new tabs, and real
page thumbnails. Android Components `SessionStorage` restores regular tab order,
selection, URLs, and navigation history after process death; private tabs and
their thumbnails are never persisted.

It also supports address-or-search navigation, persistent
history/bookmarks/folders, Android system downloads, persistent engine
preferences, default-private startup, signed extension installation and
management, and CameraX QR scanning. An in-session private-mode switch,
passwords, sync, Agent, and MCP are not yet implemented.
