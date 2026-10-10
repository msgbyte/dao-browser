// @vitest-environment node
import {execFileSync} from 'node:child_process';
import {beforeEach, describe, expect, it, vi} from 'vitest';

import {generateGithubReleaseNotes} from '../github-release.js';

vi.mock('node:child_process', async importOriginal => ({
  ...await importOriginal<typeof import('node:child_process')>(),
  execFileSync: vi.fn(),
}));

const git = vi.mocked(execFileSync);
const repository = 'https://github.com/msgbyte/dao-browser';
const sha = 'a'.repeat(40);

describe('commit-based release notes', () => {
  beforeEach(() => {
    git.mockReset();
    git.mockImplementation((_command, args) => {
      switch (args?.[0]) {
        case 'rev-parse': return 'false\n';
        case 'tag': return 'v1.0.110\nv1.0.109\nv1.0.108\nandroid-v0.1.9\n';
        case 'log': return `${sha}\tfix(sidebar): preserve tabs committed without a PR\n`;
        default: throw new Error(`Unexpected git command: ${args}`);
      }
    });
  });

  it('includes direct commits from the previous desktop version with commit and compare links', () => {
    expect(generateGithubReleaseNotes('v1.0.109')).toBe(
        `## What's Changed\n\n` +
        `* fix(sidebar): preserve tabs committed without a PR ([aaaaaaa](${repository}/commit/${sha}))\n\n` +
        `**Full Changelog**: [v1.0.108...v1.0.109](${repository}/compare/v1.0.108...v1.0.109)\n`);
    expect(git).toHaveBeenCalledWith('git', [
      'tag', '--merged', 'refs/tags/v1.0.109', '--list', 'v[0-9]*',
      '--sort=-version:refname',
    ], expect.anything());
    expect(git).toHaveBeenCalledWith('git', [
      'log', '--no-merges', '--format=%H%x09%s',
      'refs/tags/v1.0.108..refs/tags/v1.0.109', '--',
    ], expect.anything());
  });

  it('uses the tagged history for the first desktop release even when Android tags exist', () => {
    git.mockImplementation((_command, args) => {
      if (args?.[0] === 'rev-parse') return 'false\n';
      if (args?.[0] === 'tag') return 'v1.0.15\nandroid-v0.1.1\n';
      return `${sha}\tfeat: initial release\n`;
    });

    const notes = generateGithubReleaseNotes('v1.0.15');
    expect(notes).toContain('feat: initial release');
    expect(notes).toContain(`**Full Changelog**: ${repository}/commits/v1.0.15`);
    expect(git).toHaveBeenCalledWith('git', [
      'log', '--no-merges', '--format=%H%x09%s', 'refs/tags/v1.0.15', '--',
    ], expect.anything());
  });

  it('refuses shallow history instead of silently publishing incomplete notes', () => {
    git.mockReturnValueOnce('true\n');
    expect(() => generateGithubReleaseNotes('v1.0.109')).toThrow(/full git history/i);
  });

  it('propagates git failures instead of treating them as an initial release', () => {
    git.mockImplementationOnce(() => { throw new Error('git history unavailable'); });
    expect(() => generateGithubReleaseNotes('v1.0.109')).toThrow('git history unavailable');
  });

  it('rejects a missing release tag', () => {
    expect(() => generateGithubReleaseNotes('v1.0.111')).toThrow(/tag.*not found/i);
  });

  it('rejects non-desktop tags before reading git', () => {
    expect(() => generateGithubReleaseNotes('android-v0.1.9')).toThrow(/Invalid release tag/);
    expect(git).not.toHaveBeenCalled();
  });
});
