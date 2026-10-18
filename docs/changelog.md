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

Release notes are for ordinary browser users. Use plain language and focus on what
changes for them. Do not mention added regression tests, test coverage, or
verification work, even alongside a user-facing fix; describe the problem users
encountered and the resulting behavior instead.

For changes that affect users, add a concise, human-readable bullet under the
affected platform's `Unreleased` section in both `en.md` and `zh-CN.md`. This includes
features, behavior changes, bug fixes, performance, security, compatibility,
installation, and updates. Explain the user-facing outcome, not just file names or
commit hashes. Keep matching entries in the same order. Shared user-facing changes
belong to the platforms they affect; website and repository-wide changes default
to desktop when an entry is needed.

Skip entries for development-only work with no user-facing impact, such as tests,
internal refactors, CI, local build tooling, developer documentation, or agent
instructions. If such work also changes the shipped product or users' installation
or update experience, describe only that user-facing outcome. Do not add an entry
merely to record a development change or explain why no entry was needed.

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

`npm run release` archives desktop notes when it bumps `dao.json`, before packaging.
The existing transaction restores the notes on failure. As with the other desktop
release metadata, commit the archived files after a successful release. Dry runs,
`--skip-bump`, Windows attachments, and standalone package commands do not archive
notes. If manually bumping a version, archive its notes in the same change before
using `--skip-bump`.

`npm run release:android` includes archived Android notes in its version commit.
A failed version/changelog write or rejected release commit restores the version
and affected locales without overwriting concurrent edits. Tag and push failures
preserve the committed version and archived notes for retry. iOS currently has no
public-release CLI; archive its notes in the same change as a public version bump.
TestFlight builds alone do not mark notes as publicly released.

Generate other configured languages manually:

```sh
OPENAI_API_KEY=... sh ./i18n.sh --only changelog
OPENAI_API_KEY=... sh ./i18n.sh --only changelog --langs ja,zh-TW
```

The root translation command also includes changelogs by default. English and
Simplified Chinese are never overwritten, including with `--force`. Generated
Markdown retains hidden source hashes so unchanged translations survive version
archival, while edited English text gets translated again. Do not hand-edit those
generated files or remove their hashes. `--dry-run` makes no API calls or writes.
Translations use the existing OpenAI configuration and cost money; release commands
never invoke translation. Refresh existing generated locales before releasing;
stale or incomplete pending translations stop archival before any file is changed.
