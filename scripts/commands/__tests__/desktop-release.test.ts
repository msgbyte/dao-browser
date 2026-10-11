// @vitest-environment node
import {readFileSync} from 'node:fs';
import {describe, expect, it} from 'vitest';
import {plannedReleaseSteps, releaseCommand} from '../release.js';

describe('combined desktop release', () => {
  it('defaults the CLI to both platforms and exposes a Mac-only script', () => {
    expect(releaseCommand.options.find(option => option.long === '--platform')?.defaultValue).toBe('all');
    expect(JSON.parse(readFileSync('package.json', 'utf8')).scripts['release:mac'])
      .toBe('tsx scripts/cli.ts release --platform mac');
  });
  it('finishes both packages before signing metadata and uploading', () => {
    expect(plannedReleaseSteps({platform: 'all'})).toEqual([
      {phase: 'import', platform: 'mac'}, {phase: 'build', platform: 'mac'},
      {phase: 'package', platform: 'mac'}, {phase: 'notarize', platform: 'mac'},
      {phase: 'staple', platform: 'mac'}, {phase: 'import', platform: 'windows'},
      {phase: 'build', platform: 'windows'}, {phase: 'package', platform: 'windows'},
      {phase: 'update-manifest'}, {phase: 'appcast'}, {phase: 'upload'},
      {phase: 'metadata'}, {phase: 'tag'},
    ]);
  });
  it('does not rebuild or upload when both are skipped', () => {
    expect(plannedReleaseSteps({platform: 'all', skipBuild: true, skipUpload: true}).map(step => step.phase))
      .toEqual(['update-manifest', 'appcast', 'metadata', 'tag']);
  });
  it('retains independent Mac and Windows workflows', () => {
    expect(plannedReleaseSteps({platform: 'windows'}).map(step => step.phase))
      .toEqual(['import', 'build', 'package', 'upload', 'metadata', 'tag']);
    expect(plannedReleaseSteps({platform: 'mac'}).map(step => step.phase))
      .toEqual(['import', 'build', 'package', 'notarize', 'staple', 'appcast', 'metadata', 'upload', 'tag']);
  });
});
