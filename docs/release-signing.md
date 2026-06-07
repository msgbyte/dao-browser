# Release Signing & Notarization

This document is both:

1. **A release checklist** — what to do, in order, every time you ship a new
   version. Start at [Quick release checklist](#quick-release-checklist).
2. **A mechanism reference** — why each step exists and how it interacts with
   Apple's code-signing / notarization / Sparkle. Use the section TOC below.

The same pipeline is exposed via `npm run package:*` and is designed to be
reused by CI in later phases.

---

## Quick release checklist

Use this every time you cut a new version. Each step links to the deeper
explanation later in the document.

### Pre-flight (one-time per machine, done once)

These are setup tasks that survive across all releases. If you've already
done them on this machine, skip to the per-release checklist below.

- [ ] Apple Developer Program account ($99/year) — see
  [Prerequisites](#prerequisites-for-packagesigned-and-packagerelease).
- [ ] Developer ID Application certificate installed in login keychain.
- [ ] `branding/BRANDING` has the real `MAC_TEAM_ID=`.
- [ ] `xcrun notarytool store-credentials dao-notary ...` run once
  ([Option A](#option-a-keychain-profile-recommended)).
- [ ] Environment variables exported (e.g. in `~/.zshrc`):
  ```bash
  export DAO_SIGN_IDENTITY="Developer ID Application: <Name> (<TeamID>)"
  export DAO_NOTARIZE_KEYCHAIN_PROFILE=dao-notary
  ```
- [ ] **Sparkle private key backed up offline** — see
  [Back up the private key](#-back-up-the-private-key). This is the most
  critical one. Losing it permanently breaks auto-update for every shipped
  copy of the app.

### Per-release checklist (every version)

Assume we're shipping `0.5.1`. Replace the version number throughout.

1. **Bump the version**

   Edit `dao.json`:
   ```json
   "version": {
     "display": "0.5.1"
   }
   ```
   ⚠️ Sparkle uses this number to decide "is this newer than what I have?"
   If `display` does not strictly increase, clients will silently skip the
   update. Always bump it.

2. **Write release notes**

   Create `CHANGELOG_v0.5.1.md` (or update a single `CHANGELOG.md`). The
   contents end up in the GitHub Release body and — via the appcast's
   `<sparkle:releaseNotesLink>` — in Sparkle's "Update available" dialog.

3. **Build release (NOT debug)**

   ```bash
   npm run import   # apply patches + sync src/dao to engine
   npm run build    # release build — slow, 15-30+ min the first time
   ```
   ⚠️ `npm run rebuild` and `npm run build:debug` produce the **debug**
   build (component build, no Hardened Runtime, will not pass notarization).
   For releases you must use `npm run build`.

4. **Sign + notarize + staple in one step**

   ```bash
   npm run package:release
   ```
   This runs codesign (Hardened Runtime, deep nested signing) → packages a
   `.dmg` → signs the `.dmg` → submits to Apple's notary service (waits a
   few minutes) → staples the notarization ticket. See
   [What gets signed](#what-gets-signed).

5. **Sparkle-sign the dmg and (re)generate `appcast.xml`**

   ```bash
   # First release ever? Seed appcast.xml from the template:
   #   cp branding/appcast.template.xml dist/appcast.xml
   third_party/sparkle/bin/generate_appcast dist/
   ```
   This walks every `.dmg` in `dist/`, computes its EdDSA signature using
   your keychain private key, and writes a fresh `dist/appcast.xml`. See
   [Sparkle appcast](#sparkle-appcast).

6. **Publish the GitHub Release**

   ```bash
   gh release create v0.5.1 \
     dist/dao-browser-0.5.1-mac-arm64.dmg \
     dist/appcast.xml \
     --title "Dao Browser v0.5.1" \
     --notes-file CHANGELOG_v0.5.1.md
   ```
   ⚠️ The `appcast.xml` asset must be uploaded with every release —
   `SUFeedURL` resolves to `releases/latest/download/appcast.xml`, so users
   on older versions discover the new release through this file.

7. **Verify the feed is live**

   ```bash
   curl -sL https://github.com/msgbyte/dao-browser/releases/latest/download/appcast.xml | head -50
   # Should return the <rss> document containing <item> for 0.5.1.
   ```

8. **Optional but strongly recommended: real auto-update test**

   On a separate Mac (or a different macOS user account), install the
   previous version (0.5.0). Launch it, then **Dao → Check for Updates…**
   Sparkle should:

   - find 0.5.1, validate the EdDSA signature,
   - download the new `.dmg`,
   - install it on the next quit (or immediately if you click "Install"),
   - relaunch into 0.5.1.

   See [End-to-end verification](#end-to-end-verification) for diagnostics
   when this step fails.

### Silent-update path (what users actually experience)

After a release ships, the user does **nothing**. Because we configure
`SUAutomaticallyUpdate=YES` in `Info.plist`:

```
[user is on 0.5.0]
  ↓ Sparkle background-checks SUFeedURL on startup, then every 24 hours
  ↓ discovers 0.5.1
  ↓ downloads .dmg silently
  ↓ verifies EdDSA signature against SUPublicEDKey
  ↓ stages new .app in a temp dir
  ↓ user quits Dao (manually or on macOS shutdown)
  ↓ Sparkle Updater.app atomically replaces /Applications/Dao.app
  ↓ next launch is 0.5.1
```

No UI is shown unless the signature check fails or the user explicitly
clicked "Check for Updates…".

### Pitfalls (read once, internalize forever)

- **Never re-use a version number.** Sparkle clients cache `appcast.xml`
  for several minutes. If you delete a Release and republish the same
  version, some users see stale state. Always increment.
- **Never modify a `.app` after signing.** Any post-codesign edit
  (touching `Info.plist`, adding files, changing perms) invalidates the
  signature and Gatekeeper rejects on the spot. Rebuild from scratch.
- **Order matters absolutely**: `codesign → notarize → staple →
  generate_appcast`. Running `generate_appcast` first and *then* signing
  the dmg invalidates the EdDSA signature; clients refuse to install.
- **Never lose the Sparkle private key.** No recovery path other than
  shipping a manually-installed new version with a new public key — and
  the transition takes years because users have to do it by hand. See
  [Back up the private key](#-back-up-the-private-key).
- **Never use HTTP for `SUFeedURL`.** Sparkle 2.x rejects insecure feeds
  outright (MITM defense).
- **Never change `SUPublicEDKey` casually.** Existing users out in the
  field have the old key compiled into their app and will refuse all
  updates signed with the new private key. If you must rotate, plan a
  multi-version overlap window.

---

## Overview

Three independent signing modes are supported:

| Mode | Command | What it does | When to use |
|------|---------|--------------|-------------|
| Ad-hoc | `npm run package:adhoc` | Local-only signature, no certificate. Will not pass Gatekeeper on other machines. | Local smoke tests. |
| Signed | `npm run package:signed` | Real Developer ID signature + Hardened Runtime + entitlements. **Not** notarized. | Internal sharing within trusted machines, or while iterating on the signing setup before paying for notary submissions. |
| Release | `npm run package:release` | Signed + notarized + stapled. Passes Gatekeeper anywhere. Required for Sparkle auto-update to work. | Public releases. |

`--zip` can be combined with any mode (e.g. `npm run package:signed -- --zip`)
to emit a `.zip` instead of a `.dmg`.

## Prerequisites for `package:signed` and `package:release`

1. Apple Developer Program membership (\$99/year).
2. A **Developer ID Application** certificate installed in your login keychain.
   Verify with:

   ```bash
   security find-identity -v -p codesigning
   ```

   You should see a line like:

   ```
   1) ABCDEF1234... "Developer ID Application: Foo Bar (TEAMID1234)"
   ```

3. Set the signing identity environment variable (the full quoted string above
   minus the leading hash):

   ```bash
   export DAO_SIGN_IDENTITY="Developer ID Application: Foo Bar (TEAMID1234)"
   ```

4. Update `branding/BRANDING` so `MAC_TEAM_ID=` matches your Team ID.

## Additional prerequisites for `package:release` (notarization)

You need an **app-specific password** for your Apple ID
(<https://account.apple.com> → Sign-In and Security → App-Specific Passwords).

Two ways to provide credentials. The keychain profile is recommended — the
password never appears in shell history or environment dumps.

### Option A: keychain profile (recommended)

Run this once on your machine:

```bash
xcrun notarytool store-credentials dao-notary \
  --apple-id "you@example.com" \
  --team-id "TEAMID1234" \
  --password "app-specific-password"
```

Then:

```bash
export DAO_NOTARIZE_KEYCHAIN_PROFILE=dao-notary
```

### Option B: raw env vars (useful in CI)

```bash
export DAO_NOTARIZE_APPLE_ID="you@example.com"
export DAO_NOTARIZE_TEAM_ID="TEAMID1234"
export DAO_NOTARIZE_PASSWORD="app-specific-password"
```

The package script prefers `DAO_NOTARIZE_KEYCHAIN_PROFILE` if present.

## End-to-end verification

After running `npm run package:release`, verify the artifact is launchable
on a fresh machine without prompts:

```bash
spctl --assess --type install -vv dist/dao-browser-<version>-mac-arm64.dmg
# Expected: ".dmg: accepted"

# After mounting the dmg and dragging Dao.app to /Applications:
spctl --assess --type execute -vv /Applications/Dao.app
# Expected: "/Applications/Dao.app: accepted, source=Notarized Developer ID"
```

If `spctl` says `rejected`, run `xcrun stapler validate` on the dmg / app to
see whether the ticket attached, and check the notarization log via
`xcrun notarytool log <submission-id>`.

## What gets signed

The packaging script walks the built `Dao.app` and signs every nested signable
item before signing the outer bundle:

- `*.app` (helper apps) — signed with `branding/mac/dao_helper.entitlements`
- `*.framework`, `*.dylib`, `*.so`, `*.xpc` — signed with no entitlements
- The outer `Dao.app` — signed with `branding/mac/dao.entitlements`

Items are ordered deepest-first so children are sealed before their parents.
Apple has explicitly recommended against `codesign --deep`, so we do not use
it for the real signing pass. (`--sign` ad-hoc still uses `--deep` because
those signatures are not validated against any policy.)

## Entitlements rationale

`branding/mac/dao.entitlements` enables:

- `com.apple.security.cs.allow-jit` — V8 JIT pages.
- `com.apple.security.cs.allow-unsigned-executable-memory` — fallback paths
  in V8 / native modules.
- `com.apple.security.cs.disable-library-validation` — required for Chromium
  helper bundles and (later) for loading `Sparkle.framework` from
  `Dao.app/Contents/Frameworks/`.
- `com.apple.security.cs.allow-dyld-environment-variables` — Sparkle's
  relauncher passes `DYLD_*` to its child process during update install.
- Network / camera / mic / location / files / print — standard browser needs.

Helpers get a stricter set in `dao_helper.entitlements` (no
network/files/devices because those are gated by the main process).

## Common failures

| Symptom | Cause | Fix |
|---------|-------|-----|
| `code object is not signed at all` | Hardened Runtime app launched after editing `Dao.app` contents post-signature | Rebuild and resign; never modify a signed `.app`. |
| `errSecInternalComponent` during codesign | No Developer ID identity in keychain, or wrong keychain unlocked | `security unlock-keychain login.keychain-db` then retry. |
| `notarytool` returns `Invalid` | Hardened Runtime missing, or a nested item not signed | Run `npm run package:signed` and inspect `codesign -dv --verbose=4 Dao.app`. |
| `spctl: rejected` after notarize | Forgot to staple, or stapler not run after re-signing | Run `npm run package:release` end-to-end (don't reuse a stale dmg). |

## Sparkle keys

Sparkle verifies every update by checking its EdDSA signature against the
`SUPublicEDKey` stored in the app bundle's `Info.plist`. The matching
private key lives in your login keychain.

### Initial setup (one-time, done already)

```bash
npm run sparkle:fetch        # downloads Sparkle.framework + bin/ tools
npm run sparkle:keygen       # generates EdDSA keypair, prints public key
```

`sparkle:keygen` stores the private key under the keychain item
`https://sparkle-project.org` and prints the public key. The public key
has been pasted into
`src/patches/chrome/app/app-Info.plist.patch` as the value of
`SUPublicEDKey`. **Do not rotate the public key** unless you also rotate
the private key in your keychain — the two have to match or all clients
will refuse every future update.

### ⚠️ Back up the private key

If the developer machine dies and the keychain is lost, **every shipped
copy of Dao Browser becomes un-updatable forever**. There is no recovery
short of shipping a manual reinstall with a new public key, which users
have to perform by hand. Back up now:

```bash
# Export the private key as base64. Keep this string offline (1Password,
# encrypted USB stick, paper safe, etc.) — anyone with it can ship a
# malicious update that all your users will silently install.
security find-generic-password -a ed25519 -s "https://sparkle-project.org" -w \
  | tee dao-sparkle-private-key.b64

# To restore on a new machine:
security add-generic-password -a ed25519 -s "https://sparkle-project.org" \
  -w "$(cat dao-sparkle-private-key.b64)"
```

The private key is the **single most security-sensitive secret** in the
Dao Browser release process. Notarization keys / Apple ID password are
recoverable; this one is not.

### Signing a release artifact

The Sparkle `<item>` in `appcast.xml` has a `sparkle:edSignature` attribute
that authenticates the `.dmg` enclosure. Compute it with:

```bash
npm run sparkle:sign -- dist/dao-browser-0.5.1-mac-arm64.dmg
# Outputs e.g. sparkle:edSignature="MEUCIQ..." length="123456789"
```

The output line is what you paste into the `<enclosure>` element. In the
final release pipeline (Phase 2) `generate_appcast` does this for you
automatically.

## Sparkle appcast

`appcast.xml` is the feed file clients poll. It lives at the URL set by
`SUFeedURL` in `Info.plist` (currently
`https://github.com/msgbyte/dao-browser/releases/latest/download/appcast.xml`,
which auto-resolves to the asset on your latest GitHub Release).

### Maintenance model

- `branding/appcast.template.xml` is the version-controlled handwritten
  template — header, channel metadata, schema documentation. It does
  **not** ship to users.
- `dist/appcast.xml` is the generated feed that gets uploaded as a
  release asset. Do not hand-edit it.
- `third_party/sparkle/bin/generate_appcast dist/` scans every `.dmg`
  in `dist/`, signs each, and rewrites `dist/appcast.xml`. Use this
  for every release.

### One-shot publish

```bash
# Build + sign + notarize + staple a new version
npm run package:release

# Sign the dmg for Sparkle and regenerate appcast.xml
third_party/sparkle/bin/generate_appcast dist/

# Upload to a GitHub Release tagged v0.5.1
gh release create v0.5.1 \
  dist/dao-browser-0.5.1-mac-arm64.dmg \
  dist/appcast.xml \
  --title "v0.5.1" \
  --notes-file CHANGELOG_v0.5.1.md
```

Phase 2 (CI/CD) wraps this in a GitHub Actions workflow.

### Verifying end-to-end

After cutting a release:

1. Install the **previous** version on a clean test machine (or in a VM).
2. Wait for `SUScheduledCheckInterval` (24 hours by default), or use
   `Dao → Check for Updates...` to force a check.
3. Sparkle should download → verify the EdDSA signature → relaunch into
   the new version on the next quit.

If verification fails, check `~/Library/Logs/Sparkle.log` (Sparkle 2.x
writes diagnostic info there). The two most common failure modes are:

- **Signature mismatch**: the dmg in `dist/` was re-signed with codesign
  *after* `generate_appcast` ran, invalidating the EdDSA signature.
  Always sign in this order: codesign → notarize → staple →
  generate_appcast.
- **`SUFeedURL` 404**: GitHub Release was published as draft, so
  `releases/latest` doesn't include it yet.

## Next phases

- **Phase 2**: Run this pipeline from GitHub Actions on every tagged
  release, upload the `.dmg` + Sparkle `appcast.xml` to GitHub Releases
  automatically.
