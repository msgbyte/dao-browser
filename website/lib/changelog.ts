import { readFile } from 'node:fs/promises';
import { join } from 'node:path';

export type ChangelogLocale = 'en' | 'zh-CN';
export type ChangelogPlatformId = 'desktop' | 'android' | 'ios';

export interface ChangelogRelease {
  version: string;
  date: string;
  entries: string[];
}

export interface ChangelogPlatform {
  id: ChangelogPlatformId;
  title: string;
  releases: ChangelogRelease[];
}

const platformIds: ChangelogPlatformId[] = ['desktop', 'android', 'ios'];

export function parseChangelog(source: string): ChangelogPlatform[] {
  const platforms: ChangelogPlatform[] = [];
  let platform: ChangelogPlatform | undefined;
  let release: ChangelogRelease | undefined;
  const pending = new Set<string>();
  let hasTitle = false;

  for (const [index, raw] of source.split(/\r?\n/).entries()) {
    const line = raw.trimEnd();
    if (!line) continue;
    const fail = (message: string): never => {
      throw new Error(`Invalid changelog at line ${index + 1}: ${message}`);
    };
    if (!hasTitle) {
      if (!/^# \S/.test(line)) fail('expected a document title');
      hasTitle = true;
      continue;
    }
    const heading = /^## \[([^\]]+)\] (.+)$/.exec(line);
    if (heading) {
      const id = platformIds.find((candidate) => candidate === heading[1]);
      if (!id || platforms.some((item) => item.id === id)) {
        fail('unknown or duplicate platform');
      }
      platform = { id: id!, title: heading[2], releases: [] };
      platforms.push(platform);
      release = undefined;
      continue;
    }
    if (/^### \[Unreleased\] .+$/.test(line)) {
      if (!platform || pending.has(platform.id) || platform.releases.length) {
        fail('expected one Unreleased heading before releases');
      }
      pending.add(platform!.id);
      release = { version: 'Unreleased', date: '', entries: [] };
      continue;
    }
    const version = /^### \[(\d+\.\d+\.\d+)\] - (\d{4}-\d{2}-\d{2})$/.exec(line);
    if (version) {
      if (!platform || !pending.has(platform.id)) fail('release before Unreleased');
      const date = new Date(`${version[2]}T00:00:00Z`);
      if (!Number.isFinite(date.getTime()) || date.toISOString().slice(0, 10) !== version[2]) {
        fail('invalid release date');
      }
      if (platform!.releases.some((item) => item.version === version[1])) fail('duplicate version');
      release = { version: version[1], date: version[2], entries: [] };
      platform!.releases.push(release);
      continue;
    }
    if (/^- \S/.test(line) && release) {
      release.entries.push(line.slice(2));
      continue;
    }
    fail('expected a platform, release heading, or single-line bullet');
  }

  if (platforms.length !== platformIds.length || pending.size !== platformIds.length) {
    throw new Error('Invalid changelog: each platform needs one Unreleased section');
  }
  for (const item of platforms) {
    if (item.releases.some(({ entries }) => !entries.length)) {
      throw new Error(`Invalid changelog: empty release in ${item.id}`);
    }
    item.releases.sort((a, b) => b.date.localeCompare(a.date) || b.version.localeCompare(a.version, 'en', { numeric: true }));
  }
  return platformIds.map((id) => platforms.find((item) => item.id === id)!);
}

export async function loadChangelog(locale: ChangelogLocale): Promise<ChangelogPlatform[]> {
  // Both routes are prerendered; source documents are only needed during builds.
  const source = await readFile(join(process.cwd(), '../docs/changelog', `${locale}.md`), 'utf8');
  return parseChangelog(source);
}
