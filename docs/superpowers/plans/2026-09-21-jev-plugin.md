# Jev browser task plugin

Implement the approved Downloads PRD as a bundled, optional desktop plugin.

1. Register the plugin and its four canonical settings in one native manifest.
   Publish the manifest through the existing Profile settings snapshot. Keep
   connection enablement and tool permission explicitly off by default.
2. Add the isolated Jev protocol adapter and bounded observe/choose/act loop.
   Verify completion locally, preserve partial progress, and cancel on settings
   changes. Test malformed decisions, permissions, cancellation, and budgets.
3. Extend the existing host with an atomic compact observation and guarded
   actions, cancellable network requests, and redirect rejection. Preserve the
   current Agent turn, target lease, and sequential browser tool hooks.
4. Add localized configuration and global permission controls to native settings.
   Update desktop feature documentation and the regression checklist.
5. Run focused WebUI and browser checks, Lit lint, documentation checks, and
   `npm run rebuild`. Record actual evidence and unverified live-service timing.

No Git mutations, generated vendor edits, direct engine edits, or force import.

## Verification

- Focused WebUI regression: 10 files, 94 tests passed.
- `npm run lint:lit`: 222 files, no violations. `npm run docs:check` passed.
- `npm run rebuild`: passed (1,428 steps, 10m45s).
- `node scripts/checks/jev-plugin.mjs`: isolated headless browser and local mock
  service verified defaults, expansion, validation, masking, explicit permission,
  restart persistence, and zero inference requests during settings changes.
  Guarded fill/click and local AND completion passed, as did redirect rejection,
  native request cancellation, late-response suppression, and stale permission
  rejection. Actual Chinese settings screenshots were inspected.
- A real compatible endpoint and credentials are still needed to validate live
  service compatibility and the PRD's repeated comparative performance trials.
