import {mkdtempSync, readFileSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

import {
  collectReferencedDeltaBasenames,
  injectGitCommitIntoAppcast,
  updateInfoJson,
} from '../release.js';

function tempFile(name: string, content: string): string {
  const dir = mkdtempSync(path.join(os.tmpdir(), 'dao-release-test-'));
  const file = path.join(dir, name);
  writeFileSync(file, content);
  return file;
}

describe('release helpers', () => {
  it('updates info.json fields and only rewrites version segments in platform URLs',
     () => {
    const file = tempFile('info.json', JSON.stringify({
      $schema: 'kept',
      version: '1.0.0',
      chromiumVersion: '147.0.0.0',
      releasedAt: '2026-01-01',
      platforms: {
        macArm64: {
          label: 'macOS Apple Silicon',
          url: 'https://cdn.example.com/dao-browser-1.0.0-mac-arm64.dmg',
        },
        staticUrl: {
          label: 'Static',
          url: 'https://cdn.example.com/static.dmg',
        },
      },
      default: 'macArm64',
    }, null, 2));

    updateInfoJson(file, {
      version: '1.1.0',
      chromiumVersion: '148.0.0.0',
      releasedAt: '2026-02-03',
    });

    const updated = JSON.parse(readFileSync(file, 'utf-8'));
    expect(updated).toMatchObject({
      $schema: 'kept',
      version: '1.1.0',
      chromiumVersion: '148.0.0.0',
      releasedAt: '2026-02-03',
      default: 'macArm64',
    });
    expect(updated.platforms.macArm64.url)
        .toBe('https://cdn.example.com/dao-browser-1.1.0-mac-arm64.dmg');
    expect(updated.platforms.staticUrl.url)
        .toBe('https://cdn.example.com/static.dmg');
  });

  it('stamps the matching appcast item and preserves unrelated items', () => {
    const file = tempFile('appcast.xml', `<rss version="2.0">
  <channel>
    <item>
      <title>Old</title>
      <enclosure url="https://cdn.example.com/old.dmg" length="1" />
    </item>
    <item>
      <title>Current</title>
      <enclosure url="https://cdn.example.com/current.dmg" length="2" />
    </item>
  </channel>
</rss>`);

    injectGitCommitIntoAppcast(
        file, 'current.dmg',
        'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb');

    const updated = readFileSync(file, 'utf-8');
    expect(updated).toContain(
        'xmlns:dao="https://dao.msgbyte.com/xml-namespaces/dao"');
    expect(updated).toContain(
        '<dao:gitCommit>bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb</dao:gitCommit>');
    expect(updated.match(/<dao:gitCommit>/g)).toHaveLength(1);
    expect(updated).toContain('<title>Old</title>');

    injectGitCommitIntoAppcast(
        file, 'current.dmg',
        'cccccccccccccccccccccccccccccccccccccccc');

    const replaced = readFileSync(file, 'utf-8');
    expect(replaced).not.toContain('bbbbbbbb');
    expect(replaced).toContain(
        '<dao:gitCommit>cccccccccccccccccccccccccccccccccccccccc</dao:gitCommit>');
    expect(replaced.match(/<dao:gitCommit>/g)).toHaveLength(1);
  });

  it('collects only delta enclosure basenames referenced by the appcast', () => {
    const file = tempFile('appcast.xml', `<rss><channel>
      <item>
        <enclosure url="https://cdn.example.com/full.dmg" length="1" />
        <sparkle:deltas>
          <enclosure url="https://cdn.example.com/Dao2.0-1.0.delta"
                     sparkle:deltaFrom="1.0" length="2" />
          <enclosure url="https://cdn.example.com/nested/Dao2.0-0.9.delta"
                     sparkle:deltaFrom="0.9" length="3" />
        </sparkle:deltas>
      </item>
    </channel></rss>`);

    expect(collectReferencedDeltaBasenames(file)).toEqual(new Set([
      'Dao2.0-1.0.delta',
      'Dao2.0-0.9.delta',
    ]));
  });
});
