# Dao Lite skills and consent

## Goal
Split background browsing (`dao-lite`) from explicitly requested existing-tab work (`dao-browser`), and reuse background consent for one fixed hour across connections.

## Architecture
Keep the existing CLI and Agent Space backend. Embed separate runtime guides and install both thin skills. Cache temporary background consent in the MCP service by reported client name and profile, using monotonic expiry; retain existing permanent-consent preferences. Existing-tab authorization remains separate.

## Tech
Chromium C++, GN, localized GRD/XTB resources, Markdown skills, Vitest and browser tests.

## Spec
User approved the one-hour rules and two-skill split in this conversation on 2026-10-24. Consent begins on approval, does not slide with activity, and is checked on the next operation after expiry, including on an existing connection. Disabling MCP or explicit revocation invalidates access.

## Global constraints
Work on main, preserve existing edits, only explicitly authorized git mutations, no direct engine edits. Compile only with `npm run rebuild`. Do not update live home-directory skills or enable persistent permission implicitly. English and Simplified Chinese are hand-authored; other locales are untouched.

## Tasks
- [x] Add behavioral regression coverage for consent reuse, fixed expiry, revocation, and page-scope isolation.
- [x] Implement temporary consent, localized hour/always labels, and keep permanent consent compatible.
- [x] Split entry skills and guides; embed and package both; install/remove/check both using existing managed-file safeguards.
- [x] Update feature inventory, checklist, plugin documentation, and matching changelogs.
- [x] Run focused tests, attempt the required rebuild, and review the final delta against the initial dirty workspace.

## Review focus
Expiry must not be extended by requests or reconnects. A new profile or different agent requires consent. Concurrent pending requests must not resurrect revoked grants. Skills must not claim that listing tabs discovers unapproved pages, and existing-tab sequences must keep one connection. Updating skills must preserve user-owned files and report partial installations accurately.

## Progress
- Scope and call sites inspected; initial workspace snapshot saved outside the repository.
- Execution uses the primary checkout as required by AGENTS.md; pull and atomic commits were subsequently authorized; pushing remains outside the scope.
- Four focused Vitest files passed (31 tests), documentation checks passed, both entry skills passed skill validation, and localized XML/message IDs passed static checks.
- Review fixes preserve the original profile on renewal and invalidate pending approvals before cancellation callbacks; browser regression cases cover both paths.
- `npm run rebuild` stopped because `engine/src` is absent. Chromium compilation and browser tests remain unverified.
- Existing installed skills and the running application were not changed.
