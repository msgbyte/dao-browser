# Maintaining release notes

Read the [English changelog](changelog/en.md) or [Simplified Chinese changelog](changelog/zh-CN.md).
Each language has one document, with independent desktop, Android, and iOS histories.
Historical entries are reconstructed from the repository's existing Git tags:
desktop `v1.0.15` through `v1.0.108` (92 tags) and Android `android-v0.1.1`
through `android-v0.1.9` (9 tags). No iOS release tags exist at the time of this
backfill. Version headings use plain text without links.

Each entry summarizes changes since the preceding tag for the same platform;
the first entry describes the tagged baseline. Mobile-only changes are excluded
from desktop notes, and desktop-only changes are excluded from Android notes.
Dates use the tagger's calendar date for annotated tags and the target commit's
calendar date for lightweight tags. These dates describe Git history, not verified
artifact publication times. Tags pointing to the same commit and metadata-only
releases are identified explicitly. Missing version numbers are not invented.

For every change, add a concise, human-readable bullet under the affected platform's
`Unreleased` section in both `en.md` and `zh-CN.md`. Explain the visible outcome or
maintenance impact, not just file names or commit hashes. Keep one matching bullet
per change in the same order. Shared tooling changes belong to the platforms they
affect; website and repository-wide changes default to desktop.

Write in natural, concrete language: describe what someone can now do, or the
situation that failed and what has been fixed. Give substantial features enough
detail to explain their entry point and useful behavior. Separate unrelated
changes rather than hiding them behind a broad summary. There is no fixed number
of bullets per version: cover the evidence, and leave genuinely small releases
short. When backfilling, read the same-platform tag diffs and relevant source or
feature documentation; commit titles alone can miss parts of a larger change.

Use one line per bullet. Keep the bracketed platform IDs (`desktop`, `android`,
`ios`) and `Unreleased` token unchanged; the labels after them may be localized.
Version headings use `### [1.2.3] - YYYY-MM-DD` and newest releases come first.
Leave an empty `Unreleased` section above released versions. Released entries stay
with their original version; only correct factual mistakes in place.

Archive notes manually in the same change as a public version bump. Standalone
packaging, Windows attachments, and TestFlight builds do not independently mark
notes as released.
