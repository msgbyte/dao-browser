# legacy_mv2 — Dao's Manifest V2 Support

Dao keeps Manifest V2 extensions installable and runnable by default. The
restore-deprecation flag opts back into Chromium's current restrictions.
Normal extension-management restrictions remain effective.

## Chromium 151 integration

Chromium 151 replaces `ManifestV2ExperimentManager` with `ManifestV2Handler`,
removes the remaining MV2 rollout features and availability preference, and
removes `ExtensionManagement`'s MV2 exemption methods. Restoring deleted feature
flags would not restore support: the new handler normally blocks all affected
MV2 extensions, including unpacked extensions.

Dao retains its existing `extensions.manifest_v2` preference under
`DaoMV2PrefDefaults::kAvailabilityPref`. `DefaultManifestV2Availability()` returns
2 by default and 0 when `kRestoreManifestV2Deprecation` is enabled.
`GetManifestV2Availability()` uses an explicit managed preference if present;
unmanaged user values cannot override the Dao default. The retained values are:

| Value | Behavior |
| --- | --- |
| 0 | Follow Chromium's current deprecation behavior. |
| 1 | Block affected MV2 extensions. |
| 2 | Exempt affected MV2 extensions from deprecation. |
| 3 | Exempt only extensions with forced or recommended installation policy. |

The M151 handler consults this preference after checking the extension's
manifest version, type, and component status. Policy value 3 uses the existing
`ExtensionManagementClient::GetInstallationMode()` result, including both
`force_installed` and `normal_installed`. The handler observes changes to the
availability preference, disables affected enabled extensions, and removes
only the unsupported-manifest disable reason when an extension becomes allowed.
Other disable reasons remain intact.

The old `ExtensionManifestV2Availability` enterprise policy's YAML already
limited support to Chromium 110–138, and its policy-to-pref mapping was absent
in the M150 base. Retaining the managed preference does not restore external
delivery of that retired policy. Supported `ExtensionSettings` and other
extension restrictions continue through Chromium's normal management providers.
Do not document this compatibility preference as a newly supported enterprise
policy without implementing and testing its policy delivery separately.

## Chromium 152–154 compatibility

Chromium 152 moves the MV2 scope check into `manifest_v2_util`; the Dao policy
and enablement hooks remain in `ManifestV2Handler`. Chromium 153 and 154 retain
this handler, MV2 parsing, persistent background pages, and the MV2
`webRequestBlocking` permission.

Chromium 154 enables per-context WebRequest dispatch by default. Distinct
registrations can share the parent event name, so duplicate cleanup must match
the registration filter and extra-info options and retain other registrations.
Dao preserves Chromium's process identity checks, request ownership checks,
pending-target teardown, and completion signaling. The legacy sub-event path
remains covered as well.

## Install and runtime flow

```
CRX or unpacked installer
  -> PolicyCheck
  -> StandardManagementPolicyProvider::UserMayInstall
  -> ManifestV2Handler::ShouldBlockExtensionEnable
  -> MV2 scope check + Dao availability preference
  -> remaining extension-management checks
```

The same Dao decision is used for enablement, startup disabling, re-enablement,
and handler status reporting. The optional extension ID on
`ShouldBlockExtensionInstallation()` allows callers to check policy value 3;
without an ID, that method cannot grant a forced-install exemption. Production
CRX and unpacked installation use the full extension object through the normal
policy check.

With Dao's default, MV2 unpacked extensions and CRX packages remain enabled;
blocking web requests and persistent background pages continue to use Chromium's
existing implementations. With restore-deprecation enabled and no managed
exemption, M151 rejects both CRX and unpacked MV2 installations and prevents
re-enablement. This intentionally follows M151's restriction, which is stronger
than the old soft-disable stage.

## Patch inventory

- `extensions/browser/extension_prefs.cc.patch` registers Dao's retained pref.
- `extensions/browser/manifest_v2_handler.{h,cc}.patch` connects availability to
  the new handler and observes managed-pref changes.
- `extensions/browser/api/web_request/extension_web_request_event_router.cc.patch`
  tolerates duplicate registrations during session restore, preserves M151's
  replacement logic, and releases redundant listener counts and pending requests.
- The obsolete `extensions/common/extension_features.cc.patch` and
  `chrome/browser/extensions/extension_management.cc.patch` are removed.
- The existing about-flags, flag-description, install-dialog, and build patches
  retain Dao's restore-deprecation option, localized install notice, and library
  wiring. There is no restored upstream experiment manager or rollout flag.

## Tests

The browser fixture overrides `ShouldAllowMV2Extensions()` to return false, so
Chromium's test-only MV2 exemption cannot hide a broken Dao integration. The
suite retains real unpacked/CRX installation, enabled-registry checks, a
`net::ERR_BLOCKED_BY_CLIENT` assertion for the blocking listener, and a persistent
background-page marker. It also covers manual re-enablement, rejection of both
install formats under restore-deprecation, and explicit `ExtensionSettings`
blocking over Dao's default support.

`dao_mv2_handler_unittest.cc` checks managed 0/1/2/3 behavior through the real
handler, forced/recommended policy modes, disable/re-enable registry transitions,
preservation of a user-disable reason, and a managed allow overriding the
restore flag. A duplicate-listener regression drives the router's public API
through two worker deactivations, reactivation, and an extra-headers change to
verify that redundant counts are released. The per-context regression enables
the feature explicitly, collapses exact duplicate registrations, preserves a
distinct registration under the same parent event name, and verifies that
removing the last extra-headers registration releases its count. The existing
browser-level blocking test exercises M154's default per-context dispatch.

After importing the tracked proposal and confirming compilation with
`npm run rebuild`, run the relevant freshly built tests with:

```
DaoMV2BrowserTest.*:DaoMV2BrowserTestRestoreDeprecation.*:DaoMV2ManagedPolicyBrowserTest.*:DaoMV2PrefDefaultsTest.*:DaoMV2HandlerTest.*:DaoMV2HandlerRestoreTest.*:DaoMV2PerContextWebRequestTest.*
```

## Maintenance boundaries

Each Chromium upgrade must verify that manifest parsing, persistent background
pages, and blocking web requests still exist. If an API disappears, implement
only the compatibility behavior users need under this directory and keep the
corresponding behavior test. `DaoMV2APIRouter` remains a pass-through seam today.

The existing install notice is omitted for zero-permission extensions when
Chromium skips the extension-info container. No new user-facing strings or
enterprise-policy surfaces are introduced by the M151 handler migration.
