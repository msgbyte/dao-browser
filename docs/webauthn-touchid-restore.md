# Restoring WebAuthn / Touch ID Support (PRD + Operations Guide)

## Status

**Currently disabled.** As of release 1.0.21 / 1.0.22, the
`keychain-access-groups` entitlement was removed from
`branding/mac/dao.entitlements` because it caused all signed builds to crash
on launch with AMFI errors `-67671` ("Restricted entitlements not validated")
and `-413` ("No matching profile found").

Touch ID and platform-authenticator WebAuthn flows currently fall back to
software prompts. Restoring native Touch ID requires the work described in
this document.

## Background

`keychain-access-groups` is a **restricted entitlement** in Apple terminology.
macOS AMFI (Apple Mobile File Integrity) only honors restricted entitlements
when the signature is backed by an Apple-issued provisioning profile, in one
of two flavors:

1. App Store distribution profile (not applicable — we don't ship through
   the App Store).
2. Developer ID distribution profile, with the profile embedded into the
   bundle at `Contents/embedded.provisionprofile`.

Our current pipeline is **Developer ID direct-sign without an embedded
profile**, which works for every non-restricted entitlement (JIT, library
validation disable, audio/camera/location, etc.) but is incompatible with
`keychain-access-groups`. Adding the entitlement back without also shipping
a profile reproduces the launch-time crash.

## Goal

Re-enable platform-authenticator WebAuthn (Touch ID) for users without
breaking the existing direct-sign + notarize + Sparkle pipeline.

## Non-goals

- Switching to the Mac App Store. Distribution stays on dao-release.msgbyte.com
  + Sparkle.
- Supporting Intel (`x86_64`). Pipeline remains `mac-arm64` only.
- Touch ID inside the Debug build. Dev builds keep using the software
  fallback (see [Debug build behavior](#debug-build-behavior)).

## Success criteria

- A user-installed 1.0.23 (or whichever release ships this) bundle launches
  cleanly on macOS 12+ — no `EXC_CRASH (SIGKILL Code Signature Invalid)`,
  no AMFI rejection in `log show`.
- `codesign -d --entitlements -` on the installed app shows the
  `keychain-access-groups` array with the correct group string.
- `codesign -d --verbose=4` shows an `embedded.provisionprofile` blob.
- Visiting a site with `navigator.credentials.create({ publicKey: { authenticatorSelection: { authenticatorAttachment: 'platform' } } })`
  prompts the Touch ID sensor instead of the software fallback.
- Sparkle delta updates from 1.0.20 → new version succeed and the resulting
  bundle still launches.

## Prerequisites

- Apple Developer Program membership (already active — Team ID `ZV5X3A7263`).
- Apple ID with access to https://developer.apple.com/account
  (currently `moonrailgun@gmail.com`).
- Existing Developer ID Application certificate in login keychain
  (already present — used by the current pipeline).
- Codesigning identity env var already set: `DAO_SIGN_IDENTITY="Developer ID Application: liang chen (ZV5X3A7263)"`.

---

## Implementation Plan

The work splits into four sequential phases. Each phase has an explicit
verification step — do not move on until the verification passes.

### Phase 1 — Apple Developer portal: register App ID with Keychain Sharing

This phase is pure clickops on developer.apple.com. No code changes.

**Steps**

1. Open https://developer.apple.com/account/resources/identifiers/list.
2. Filter for "App IDs" (default view).
3. Search for `com.msgbyte.dao`:
   - **If it exists**: click it, jump to step 5.
   - **If it doesn't exist**: click the `+` button, choose `App IDs` →
     Continue → `App` → Continue.
4. New App ID form:
   - Description: `Dao Browser`
   - Bundle ID: select `Explicit`, value `com.msgbyte.dao`
   - In the Capabilities list, **check `Keychain Sharing`**.
   - Continue → Register.
5. (Modifying an existing App ID) In the Capabilities table, check
   `Keychain Sharing`. Click `Configure` on its row. Add one Keychain Group:
   `$(AppIdentifierPrefix)com.msgbyte.dao`. Save.

   Apple will warn that modifying the App ID invalidates existing
   provisioning profiles. This is fine — we have none yet.

6. (Optional) Repeat steps 3–5 for `com.msgbyte.dao.debug` if Touch ID is
   ever wanted in Debug builds. **Recommended: skip this.** See
   [Debug build behavior](#debug-build-behavior).

**Verification**

In the Identifiers list, `com.msgbyte.dao` shows `Keychain Sharing` in the
Capabilities column.

### Phase 2 — Generate a Developer ID provisioning profile

**Critical**: the profile type must be **Developer ID**, not Development,
not App Store, not Ad Hoc. Apple's UI offers six options; only "Developer ID"
works for direct-distributed notarized builds.

**Steps**

1. Open https://developer.apple.com/account/resources/profiles/list.
2. Click `+` to create a new profile.
3. Under "Distribution", choose **`Developer ID`** → Continue.
4. Profile Type: select `Mac` → Continue.
5. App ID: choose `com.msgbyte.dao` (the one from Phase 1) → Continue.
6. Certificates: select the Developer ID Application certificate currently
   used for signing (the one matching `DAO_SIGN_IDENTITY`). If multiple
   certs are listed, pick the one whose expiry matches the cert in your
   login keychain — `security find-identity -v -p codesigning` will show
   the local cert's identifier.
7. Provisioning Profile Name: `Dao Browser Developer ID`.
8. Generate → Download. The file is `Dao_Browser_Developer_ID.provisionprofile`.

**Storage**

Save the downloaded file to a path NOT checked into git:

```
~/.config/dao-browser/embedded.provisionprofile
```

(Or any path the operator can reach; the env var below points at it.) Do
NOT commit the profile into the repo — it embeds the cert thumbprint and is
trivially regenerable from the portal if lost.

**Verification**

```bash
security cms -D -i ~/.config/dao-browser/embedded.provisionprofile | head -40
```

The plist should include:
- `<key>TeamIdentifier</key>` with value `ZV5X3A7263`
- `<key>Entitlements</key>` containing a `keychain-access-groups` array
- `<key>ProvisionsAllDevices</key>` either absent or `false`

### Phase 3 — Wire the profile into the build pipeline

Two code changes:

**3a. Add env var convention to release prerequisites**

In `scripts/commands/release.ts`, extend the pre-flight check so that when
`branding/mac/dao.entitlements` contains `keychain-access-groups`, the
profile path env var is required:

```ts
const PROVISION_PROFILE_ENV = "DAO_PROVISION_PROFILE";
// in pre-flight, when entitlements declare keychain-access-groups:
if (!process.env[PROVISION_PROFILE_ENV] || !existsSync(process.env[PROVISION_PROFILE_ENV])) {
  problems.push(
    `Missing or invalid ${PROVISION_PROFILE_ENV}.\n` +
    "  The entitlements template declares restricted entitlements that " +
    "require an embedded provisioning profile.\n" +
    "  Set DAO_PROVISION_PROFILE to the absolute path of the .provisionprofile file."
  );
}
```

**3b. Copy the profile into the bundle BEFORE signing**

In `scripts/commands/package.ts`, before `signAppBundle` runs, copy the
profile to `Dao.app/Contents/embedded.provisionprofile`. The copy must
happen before signing so the file is hashed into the code signature.

Sketch:

```ts
function embedProvisionProfile(appBundle: string): void {
  const profileSrc = process.env.DAO_PROVISION_PROFILE;
  if (!profileSrc) return;  // entitlements without keychain-access-groups path
  const dest = path.join(appBundle, "Contents", "embedded.provisionprofile");
  copyFileSync(profileSrc, dest);
  log(`Embedded provisioning profile → ${path.relative(ROOT_DIR, dest)}`);
}

// called from signAppBundle BEFORE the codesign loop:
embedProvisionProfile(appBundle);
```

**3c. Restore the entitlement**

In `branding/mac/dao.entitlements`, re-add the block that was removed in the
1.0.22 fix:

```xml
<key>keychain-access-groups</key>
<array>
    <string>$(AppIdentifierPrefix)$(CFBundleIdentifier)</string>
</array>
```

The existing `renderEntitlements()` machinery in `package.ts` will expand
`$(AppIdentifierPrefix)` and `$(CFBundleIdentifier)` from the signing
identity's Team ID and the bundle's `Info.plist`. No template change needed.

### Phase 4 — Verify, ship, watch

**Local verification (BEFORE notarization)**

After `npm run package:release` produces the signed `.app`:

```bash
# 1. Profile is present and hashed into signature
codesign -d --verbose=4 dist/Dao.app 2>&1 | grep -i provision
# Expect a line: "Provision profile: <hash>"

# 2. Entitlements got expanded correctly
codesign -d --entitlements - --xml dist/Dao.app | grep -A2 keychain-access
# Expect:
#   <key>keychain-access-groups</key>
#   <array>
#     <string>ZV5X3A7263.com.msgbyte.dao</string>

# 3. AMFI accepts the bundle at launch (this is the crash test for 1.0.21/22)
open dist/Dao.app
# App must open. If it crashes, check Console.app for AppleMobileFileIntegrity
# log entries before doing anything else.
```

**If launch crashes**: STOP. Do not notarize. Do not upload. Capture AMFI
logs and diff against
[Diagnosing failures](#diagnosing-failures) below.

**Full release**

Once local launch passes:

```bash
npm run release
```

The standard pipeline applies — sign, notarize, staple, generate appcast,
upload to R2. Nothing changes from the operator's perspective.

**Production verification**

After Sparkle pushes the update to a test machine:

1. App launches without prompts.
2. Visit https://webauthn.io (or similar) → create credential with platform
   authenticator → Touch ID sensor lights up, not a password prompt.
3. `log show --predicate 'sender == "AppleMobileFileIntegrity"' --last 10m`
   shows no entries for `Dao`.

---

## Debug build behavior

Bundle ID for `npm run build:debug` is `com.msgbyte.dao.debug`. To enable
Touch ID in Debug, Phase 1 + 2 must be repeated for that bundle ID.

**Recommended**: don't bother. Reasons:

- Debug iteration speed matters more than Touch ID parity. Embedding a
  profile adds a step to every dev build.
- The `keychain-access-groups` entitlement template uses
  `$(CFBundleIdentifier)`, so debug builds would silently request
  `ZV5X3A7263.com.msgbyte.dao.debug` and AMFI would crash them unless
  there's a separate `.provisionprofile` for the debug bundle id.
- Software fallback works fine for testing WebAuthn flows in dev.

Implementation guard: the `embedProvisionProfile` helper in Phase 3b should
silently no-op when `DAO_PROVISION_PROFILE` is unset. The pre-flight check
in Phase 3a should only require the env var for `package:release` (the
notarize path), not for `package:debug`.

---

## Profile lifecycle

- **Expiry**: Developer ID provisioning profiles last for ~1 year. Apple
  emails before expiry.
- **Already-shipped builds are unaffected by expiry**: macOS only checks
  the profile during code-signature validation, not against an online
  expiry server. A build signed today keeps working forever (as long as
  the underlying Developer ID certificate is valid).
- **Renewing**: download a new profile from the portal, replace
  `~/.config/dao-browser/embedded.provisionprofile`. Next `npm run release`
  embeds the new profile.
- **Certificate rotation**: when the Developer ID Application cert is
  rotated, the profile must be regenerated against the new cert (Phase 2
  step 6). Old shipped builds keep working under the old cert; new builds
  ship under the new cert + new profile.

---

## Sparkle compatibility

`embedded.provisionprofile` is a normal file inside `Dao.app/Contents/`.
It participates in:

- Code signature: yes (hashed into the signature, so any tamper invalidates
  the signature — this is what AMFI checks).
- Binary delta: yes. `generate_appcast` produces deltas that include the
  file. As long as both old and new versions ship a valid profile,
  Sparkle's delta apply succeeds.

**Edge case**: shipping 1.0.20 → 1.0.23-with-profile via delta. The 1.0.20
bundle has no profile; the delta will add the file. AMFI on 1.0.23 will
validate the resulting bundle against the new signature, which includes the
profile hash, so this is safe. Verify by installing 1.0.20 first, then
letting Sparkle auto-update to the new release in a test profile.

---

## Diagnosing failures

When a signed bundle crashes on launch with `EXC_CRASH (SIGKILL Code Signature Invalid)`:

```bash
# Capture AMFI's reasoning
log show --predicate 'sender == "AppleMobileFileIntegrity"' --last 10m

# Look for one of:
#   "Restricted entitlements not validated, error -67671"     → profile missing or wrong
#   "No matching profile found, error -413"                    → entitlement declares group not authorized by profile
#   "code signature validation failed"                          → signature itself broken (post-sign tampering, copy in wrong order)
```

Likely cause matrix:

| AMFI error                              | Likely root cause                                                                          |
| --------------------------------------- | ------------------------------------------------------------------------------------------ |
| `-67671 / -413 No matching profile`     | `embedded.provisionprofile` missing from bundle, or copied in AFTER signing (not hashed). |
| `-67671` without `-413`                 | Profile present but its authorized `keychain-access-groups` doesn't match entitlements.    |
| `code signature validation failed`      | Something modified the bundle after signing (post-sign asset injection, lipo, etc.).       |
| `Library not loaded: ...Sparkle.framework` | Unrelated — re-check `cs.disable-library-validation` entitlement. Not a profile issue.  |

To inspect what's actually in the shipped bundle:

```bash
# What's the bundle declaring it needs?
codesign -d --entitlements - --xml /Applications/Dao.app

# What profile is embedded?
security cms -D -i /Applications/Dao.app/Contents/embedded.provisionprofile

# What groups does the embedded profile authorize?
security cms -D -i /Applications/Dao.app/Contents/embedded.provisionprofile \
  | plutil -extract Entitlements.keychain-access-groups xml1 -o - -
```

The entitlements `keychain-access-groups` array must be a subset of the
profile's `keychain-access-groups` array. Mismatch → AMFI rejects.

---

## Rollback

If a release with the profile turns out to crash on some macOS version we
didn't test:

1. Remove `keychain-access-groups` from `branding/mac/dao.entitlements`
   (revert Phase 3c).
2. Remove the `embedProvisionProfile` call from `package.ts`
   (revert Phase 3b).
3. Cut a new release. Touch ID falls back to software prompt; the rest of
   the browser continues to work.

The hostile-to-rollback step is the **portal-side App ID modification**
from Phase 1 — you can't easily un-enable Keychain Sharing without
invalidating any future profiles. But you don't need to: as long as the
entitlement isn't declared in the build, AMFI doesn't care what the
portal says.

---

## References

- Apple: [Embedding a provisioning profile in a Mac app](https://developer.apple.com/documentation/security/notarizing_macos_software_before_distribution/customizing_the_notarization_workflow)
- Apple: [Hardened Runtime entitlements](https://developer.apple.com/documentation/security/hardened_runtime)
- Apple: [Keychain Sharing capability](https://developer.apple.com/documentation/xcode/configuring-keychain-sharing)
- Internal: `docs/release-signing.md` — full signing pipeline.
- Internal: `branding/mac/dao.entitlements` — current entitlement list and
  the NOTE comment that points back to this doc.
