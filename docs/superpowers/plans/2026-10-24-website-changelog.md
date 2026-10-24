# Website Changelog Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. Keep changes uncommitted in the primary checkout as required by AGENTS.md.

**Goal:** Publish a readable release timeline from the existing changelog documents, without maintaining another copy of release content.

**Architecture:** Statically render `/changelog` from `docs/changelog/en.md` and `/changelog/zh-CN` from the existing Chinese document. A small client component switches platform sections and follows version anchors. Website locale files contain interface copy only.

**Tech Stack:** Next.js, React, TypeScript, CSS modules, Vitest; no new dependencies.

**Spec:** User-approved design: dark blue timeline, desktop/Android/iOS switching, version/date metadata, responsive single-column layout, navigation/footer links, permanent version anchors, released entries only, and an empty state for platforms without releases.

## Global Constraints

- Keep `docs/changelog/*.md` as the only maintained release-content source.
- Preserve source wording, links, and inline code. Do not infer categories or generate summaries.
- Fail the build on malformed document structure instead of silently dropping content.
- Localize new interface copy in English and Simplified Chinese.
- Preserve existing work; do not create branches, commits, or deployments.

## Review Focus

- Pending entries never appear in rendered release data.
- Missing/duplicate platforms, duplicate versions, malformed dates, and unexpected blocks fail clearly.
- Unsafe Markdown links and HTML remain inert.
- Direct links to non-default platforms and browser history reveal the intended release.
- Both static routes build with documents outside the website directory.

## Task 1: Source reader and safe inline formatting

- [x] Add `website/lib/changelog.ts`: `parseChangelog(source: string)` returns platform IDs/titles and released versions/dates/entries; `loadChangelog(locale)` reads canonical files during the build.
- [x] Add `website/lib/__tests__/changelog.test.ts`; cover pending isolation, sorting, real English/Chinese documents, invalid structure, and duplicate identifiers. Run the test before and after implementation.
- [x] Add a small `InlineMarkdown` component and tests for code, safe links, and inert HTML/unsafe links.

## Task 2: Static pages and platform navigation

- [x] Add `website/app/changelog/page.tsx`, `zh-CN/page.tsx`, shared page/timeline components, and `changelog.module.css`.
- [x] Add interface-only `website/locales/{en,zh-CN}/changelog.json` files.
- [x] Verify platform switching, direct version anchors, history navigation, empty states, and language links with focused React DOM tests.
- [x] Connect TopNav, Footer, and sitemap; preserve the existing download history page.

## Task 3: Deployment, documentation, and validation

- [x] Watch `docs/changelog/**` in website push and PR deployment triggers.
- [x] Document the source/build contract in `docs/changelog.md`, inventory, and checklist; add matching user-facing pending changelog entries.
- [x] Run focused website tests, website type check/lint/build, documentation checks, and `git diff --check`.
- [x] Inspect the built HTML for both languages, stable version anchors, and exclusion of pending entries. Review the final change before delivery.
