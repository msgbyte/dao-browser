import {spawnSync} from 'node:child_process';
import {
  chmodSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

import {
  buildGithubReleasePlan,
  collectGithubReleasePlans,
  publishGithubRelease,
} from '../github-release.js';

function installFakeReleaseTools(releaseJson: string) {
  const binDir = mkdtempSync(path.join(os.tmpdir(), 'dao-release-tools-'));
  const logPath = path.join(binDir, 'commands.log');
  const tool = `#!/bin/sh
printf '%s' "$(basename "$0")" >> "$DAO_GITHUB_RELEASE_TEST_LOG"
printf ' %s' "$@" >> "$DAO_GITHUB_RELEASE_TEST_LOG"
printf '\n' >> "$DAO_GITHUB_RELEASE_TEST_LOG"
if [ "$(basename "$0")" = "gh" ] && [ "$1" = "release" ] && [ "$2" = "view" ]; then
  printf '%s\n' "$DAO_GITHUB_RELEASE_VIEW_JSON"
fi
`;
  for (const name of ['gh', 'curl']) {
    const toolPath = path.join(binDir, name);
    writeFileSync(toolPath, tool);
    chmodSync(toolPath, 0o755);
  }

  const previousPath = process.env.PATH;
  const previousLog = process.env.DAO_GITHUB_RELEASE_TEST_LOG;
  const previousJson = process.env.DAO_GITHUB_RELEASE_VIEW_JSON;
  process.env.PATH = `${binDir}:${previousPath ?? ''}`;
  process.env.DAO_GITHUB_RELEASE_TEST_LOG = logPath;
  process.env.DAO_GITHUB_RELEASE_VIEW_JSON = releaseJson;

  return {
    readCommands: () => readFileSync(logPath, 'utf-8').trim().split('\n'),
    restore: () => {
      process.env.PATH = previousPath;
      if (previousLog === undefined) {
        delete process.env.DAO_GITHUB_RELEASE_TEST_LOG;
      } else {
        process.env.DAO_GITHUB_RELEASE_TEST_LOG = previousLog;
      }
      if (previousJson === undefined) {
        delete process.env.DAO_GITHUB_RELEASE_VIEW_JSON;
      } else {
        process.env.DAO_GITHUB_RELEASE_VIEW_JSON = previousJson;
      }
      rmSync(binDir, {recursive: true, force: true});
    },
  };
}

describe('GitHub release publishing', () => {
  it('publishes an existing draft whose asset is already uploaded', async () => {
    const assetName = 'dao-browser-1.0.101-mac-arm64.dmg';
    const tools = installFakeReleaseTools(JSON.stringify({
      isDraft: true,
      assets: [{name: assetName}],
    }));

    try {
      await publishGithubRelease(buildGithubReleasePlan('v1.0.101'));

      expect(tools.readCommands()).toEqual([
        'gh release view v1.0.101 --json assets,isDraft',
        'gh release edit v1.0.101 --draft=false',
      ]);
    } finally {
      tools.restore();
    }
  });

  it('publishes an existing draft after uploading its missing asset', async () => {
    const tools = installFakeReleaseTools(JSON.stringify({
      isDraft: true,
      assets: [],
    }));

    try {
      await publishGithubRelease(buildGithubReleasePlan('v1.0.101'));

      const commands = tools.readCommands();
      expect(commands[0]).toBe(
          'gh release view v1.0.101 --json assets,isDraft');
      expect(commands[1]).toMatch(/^curl --fail --location --retry 3 /);
      expect(commands[2]).toMatch(/^gh release upload v1\.0\.101 /);
      expect(commands[3]).toBe('gh release edit v1.0.101 --draft=false');
    } finally {
      tools.restore();
    }
  });

  it('builds resumable archive plans from full appcast enclosures', () => {
    const xml = `<rss><channel>
      <!--
      <item>
        <sparkle:shortVersionString>0.0.1.0</sparkle:shortVersionString>
        <enclosure url="https://example.com/dao-browser-0.0.1-mac-arm64.dmg" />
      </item>
      -->
      <item>
        <sparkle:shortVersionString>1.0.101.0</sparkle:shortVersionString>
        <enclosure url="https://dao-release.msgbyte.com/dao-browser-1.0.101-mac-arm64.dmg" />
        <enclosure url="https://dao-release.msgbyte.com/Dao101.0-100.0.delta"
                   sparkle:deltaFrom="100.0" />
      </item>
      <item>
        <sparkle:shortVersionString>1.0.100.0</sparkle:shortVersionString>
        <enclosure url="https://dao-release.msgbyte.com/dao-browser-1.0.100-mac-arm64.dmg" />
      </item>
    </channel></rss>`;

    expect(collectGithubReleasePlans(xml)).toEqual([
      {
        tag: 'v1.0.101',
        assetName: 'dao-browser-1.0.101-mac-arm64.dmg',
        sourceUrl:
          'https://dao-release.msgbyte.com/dao-browser-1.0.101-mac-arm64.dmg',
      },
      {
        tag: 'v1.0.100',
        assetName: 'dao-browser-1.0.100-mac-arm64.dmg',
        sourceUrl:
          'https://dao-release.msgbyte.com/dao-browser-1.0.100-mac-arm64.dmg',
      },
    ]);
  });

  it('prints the exact R2 source and GitHub target without publishing', () => {
    const result = spawnSync(
        process.execPath,
        [
          '--import',
          'tsx',
          'scripts/cli.ts',
          'github-release',
          'publish',
          'v1.0.101',
          '--dry-run',
        ],
        {cwd: process.cwd(), encoding: 'utf-8'});

    expect(result.status, result.stderr).toBe(0);
    expect(result.stdout).toContain(
        'https://dao-release.msgbyte.com/' +
        'dao-browser-1.0.101-mac-arm64.dmg');
    expect(result.stdout).toContain(
        'gh release create v1.0.101 ' +
        'dao-browser-1.0.101-mac-arm64.dmg');
  });
});
