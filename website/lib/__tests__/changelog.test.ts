import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { parseChangelog } from '../changelog';

const source = `# Changelog

## [desktop] Desktop
### [Unreleased] Unreleased
- Pending desktop change.
### [1.0.9] - 2026-10-23
- Keep \`code\` and [links](https://example.com).
### [1.0.10] - 2026-10-23
- Newer release.
## [android] Android
### [Unreleased] Unreleased
- Pending Android change.
### [0.1.0] - 2026-10-22
- Android release.
## [ios] iOS
### [Unreleased] Unreleased
- Pending iOS change.
`;

describe('changelog source', () => {
  it('keeps released wording and separates platforms, newest first', () => {
    const platforms = parseChangelog(source);
    expect(platforms.map(({ id }) => id)).toEqual(['desktop', 'android', 'ios']);
    expect(platforms[0].releases.map(({ version }) => version)).toEqual(['1.0.10', '1.0.9']);
    expect(platforms[0].releases[1].entries).toEqual(['Keep `code` and [links](https://example.com).']);
    expect(platforms[1].releases[0].entries).toEqual(['Android release.']);
    expect(platforms[2].releases).toEqual([]);
    expect(JSON.stringify(platforms)).not.toContain('Pending');
    expect(parseChangelog(source.replaceAll('\n', '\r\n'))).toEqual(platforms);
  });

  it.each(['en', 'zh-CN'])('reads every released bullet from the canonical %s document', (locale) => {
    const path = resolve(dirname(fileURLToPath(import.meta.url)), `../../../docs/changelog/${locale}.md`);
    const markdown = readFileSync(path, 'utf8');
    const platforms = parseChangelog(markdown);
    const releasedSections = markdown.split(/^### /m).filter((section) => /^\[\d/.test(section));
    expect(platforms.flatMap(({ releases }) => releases)).toHaveLength(releasedSections.length);
    expect(platforms.flatMap(({ releases }) => releases.flatMap(({ entries }) => entries))).toHaveLength(
      releasedSections.reduce((count, section) => count + (section.split(/^## /m)[0].match(/^- /gm)?.length ?? 0), 0),
    );
  });

  it.each([
    ['missing platform', source.replace(/## \[ios\][\s\S]*/, '')],
    ['duplicate platform', `${source}\n## [ios] Duplicate\n### [Unreleased] Unreleased`],
    ['unknown platform', source.replace('[ios]', '[watch]')],
    ['duplicate version', source.replace('[1.0.10]', '[1.0.9]')],
    ['invalid date', source.replace('2026-10-22', '2026-02-30')],
    ['unsupported block', source.replace('- Newer release.', 'A lost paragraph.')],
    ['indented code block', source.replace('### [1.0.10]', '    ### [1.0.10]')],
    ['nested bullet', source.replace('- Newer release.', '  - Nested release detail.')],
    ['orphan bullet', source.replace('# Changelog', '# Changelog\n- Orphan.')],
    ['missing pending heading', source.replace('### [Unreleased] Unreleased\n', '')],
    ['empty release', source.replace('- Newer release.', '')],
  ])('rejects %s instead of silently dropping content', (_name, markdown) => {
    expect(() => parseChangelog(markdown)).toThrow(/changelog/i);
  });
});
