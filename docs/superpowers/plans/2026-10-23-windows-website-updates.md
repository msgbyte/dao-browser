# Website-managed Windows Updates

**Goal:** Publish the signed Windows update feed with the website while keeping existing R2-based clients working and allowing a selected historical Windows release to be restored.

**Design:** Keep immutable installers, checksum files, and versioned signed manifests on R2. Track the exact signed latest manifest at `website/public/windows-update.json`. New clients fetch `https://dao.msgbyte.com/windows-update.json`; installer origin and signature verification remain unchanged. After a successful production deployment, verify the live website feed matches the checkout and mirror its exact bytes to the legacy R2 key. Preview deployments never mirror.

**Current incident:** The user authorized restoring the remote R2 feed to 1.0.115. The existing 1.0.115 signature, installer availability/size, and checksum sidecar were verified; the original 1.0.117 feed was backed up. R2 was replaced and the public result matched the signed 1.0.115 bytes. Seed the website feed with this same restored version.

## Tasks

- [x] Separate the native feed URL from the installer origin and assert the request URL in the updater test. Root alone runs compilation after edits are batched.
- [x] Make release upload only immutable artifacts; include the website feed in the release metadata transaction and source-provenance exclusions. Preserve original artifacts for failed-upload retries.
- [x] Add a CLI check/sync command and a production-only post-deploy legacy mirror. Require the exact deployed bytes, valid signatures, available installer, and matching versioned manifest; use explicit no-store headers for the website feed.
- [x] Extend rollback to restore the selected Windows signed manifest with website download metadata. Historical manifests missing from Git can be read from their immutable R2 URL. Preserve application versions, binaries and Mac appcast. Dry-run does not write or publish.
- [x] Update features, checklist, release/development documentation, and English/Chinese changelogs. Document the required Actions secrets without copying credentials automatically.
- [x] Run focused script tests, website checks, native compile confirmation using only npm run rebuild, and independent review. Do not publish repository changes.

## Review focus

Signed bytes must not be reconstructed; old clients cannot follow redirects. A website deployment failure must not advance the legacy feed. Rollback and sync must reject mismatched or unavailable artifacts. Recoverable failures must retain local work and avoid publishing unrelated commits. Existing user edits are out of scope.

## Verification

- Focused release, rollback, transaction, Windows metadata/publication/mirror, desktop/Windows release, and workflow tests passed (168 tests initially; the final mirror suite adds one passing corrupted-feed recovery test).
- Website download/redirect/appcast tests passed (19 tests); root/website TypeScript checks, website lint/build, documentation checks, and diff whitespace checks passed.
- `release:rollback -- --version 1.0.115 --dry-run` selected the original Mac/Windows 1.0.115 URLs without writing. `release:sync-windows-update -- --check` verified the seeded feed against live immutable R2 artifacts.
- `npm run rebuild` passed on macOS. Windows updater execution and Windows-specific unit tests still require Windows verification.
- Independent review identified that a damaged legacy feed could block its repair. Removed the unnecessary pre-upload read, added a regression test, and confirmed the fix. Main deployments are serialized through the legacy mirror step.
- The public legacy R2 feed was rechecked as signed 1.0.115. Repository changes have not been committed or deployed. Required Cloudflare/R2 Actions secrets are not configured; credentials were not copied.
