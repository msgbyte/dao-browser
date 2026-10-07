import {createHash} from 'node:crypto';
import {readFileSync, readdirSync} from 'node:fs';
import path from 'node:path';

export type ChangelogPlatform = 'desktop' | 'android' | 'ios';

export function formatReleaseDate(date: Date): string {
  const yyyy = date.getFullYear();
  const mm = String(date.getMonth() + 1).padStart(2, '0');
  const dd = String(date.getDate()).padStart(2, '0');
  return `${yyyy}-${mm}-${dd}`;
}

function pendingSection(source: string, platform: ChangelogPlatform) {
  const sections = source.split(/(?=^## \[)/m);
  const matches = sections.filter((section) => section.startsWith(`## [${platform}] `));
  if (matches.length !== 1) throw new Error(`Expected one [${platform}] changelog section.`);
  const section = matches[0];
  if ((section.match(/^### \[Unreleased\]/gm) || []).length !== 1) {
    throw new Error(`Expected one [Unreleased] section for ${platform}.`);
  }
  const match = /^(### \[Unreleased\][^\n]*\n)([\s\S]*)/m.exec(section)!;
  const next = match[2].search(/^### /m);
  const body = next < 0 ? match[2] : match[2].slice(0, next);
  if (body.split('\n').some((line) => line.trim() && !/^- \S/.test(line))) {
    throw new Error(`Use one nonempty bullet per line in ${platform} Unreleased notes.`);
  }
  return {section, heading: match[1], body};
}

/** Plan every locale before writing so malformed notes cannot partially archive. */
export function planChangelogRelease(
  root: string, platform: ChangelogPlatform, version: string, date: Date,
): Array<{file: string; contents: Buffer}> {
  if (!/^\d+\.\d+\.\d+$/.test(version)) throw new Error('Invalid changelog version.');
  const directory = path.join(root, 'docs/changelog');
  const files = readdirSync(directory).filter((file) => file.endsWith('.md')).sort();
  for (const required of ['en.md', 'zh-CN.md']) {
    if (!files.includes(required)) throw new Error(`Missing changelog: ${required}`);
  }
  const english = readFileSync(path.join(directory, 'en.md'), 'utf8');
  const entries = pendingSection(english, platform).body.match(/^- .+$/gm) || [];
  const hashes = entries.map((entry) => createHash('sha256').update(entry).digest('hex')).sort();
  return files.flatMap((name) => {
    const file = path.join(directory, name);
    const source = readFileSync(file, 'utf8');
    const {section, heading, body} = pendingSection(source, platform);
    const localizedEntries = body.match(/^- .+$/gm) || [];
    if (localizedEntries.length !== entries.length) {
      throw new Error(`${name}: ${platform} Unreleased entries differ from English. Update the changelog translations.`);
    }
    if (name !== 'en.md' && name !== 'zh-CN.md') {
      const translatedHashes = localizedEntries.map((entry) => /<!-- source:([a-f0-9]{64}) -->$/.exec(entry)?.[1]).sort();
      if (JSON.stringify(translatedHashes) !== JSON.stringify(hashes)) {
        throw new Error(`${name}: stale ${platform} notes. Run i18n.sh --only changelog before releasing.`);
      }
    }
    if (!entries.length) return [];
    if (section.includes(`### [${version}]`)) throw new Error(`${name}: ${platform} ${version} is already archived.`);
    const archived = `${heading}\n### [${version}] - ${formatReleaseDate(date)}\n\n${body.trim()}\n\n`;
    return [{file, contents: Buffer.from(source.replace(section, () => section.replace(heading + body, () => archived)))}];
  });
}
