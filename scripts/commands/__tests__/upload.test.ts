import {describe, expect, it} from 'vitest';

import {
  getAssociatedDeltaIndexes,
  getCleanupCandidates,
  getSelectableCleanupIndexes,
  parseR2ListOutput,
  uploadCommand,
} from '../upload.js';

describe('upload cleanup helpers', () => {
  it('parses wrangler R2 list JSON object arrays and result wrappers', () => {
    expect(parseR2ListOutput(JSON.stringify([
      {
        key: 'dao-browser-0.4.0-mac-arm64.dmg',
        size: 123,
        uploaded: '2026-01-01T00:00:00.000Z',
      },
    ]))).toEqual([
      {
        key: 'dao-browser-0.4.0-mac-arm64.dmg',
        size: 123,
        uploaded: '2026-01-01T00:00:00.000Z',
      },
    ]);

    expect(parseR2ListOutput(JSON.stringify({
      objects: [
        {
          key: 'releases/dao-browser-0.5.0-mac-arm64.dmg',
          size: 456,
        },
      ],
    }))).toEqual([
      {
        key: 'releases/dao-browser-0.5.0-mac-arm64.dmg',
        size: 456,
      },
    ]);
  });

  it('selects old Dao release objects while keeping the latest versions', () => {
    const candidates = getCleanupCandidates([
      {key: 'dao-browser-0.3.0-mac-arm64.dmg', size: 10},
      {key: 'dao-browser-0.4.0-mac-arm64.dmg', size: 20},
      // Sparkle delta naming: Dao<to>-<from>.delta (CFBundleVersion)
      {key: 'Dao4.0-3.0.delta', size: 5},
      {key: 'dao-browser-0.5.0-mac-arm64.dmg', size: 30},
      {key: 'Dao5.0-4.0.delta', size: 6},
      {key: 'appcast.xml', size: 40},
      {key: 'notes.txt', size: 50},
    ], 1);

    // DMGs: keep 0.5.0 → delete 0.4.0 and 0.3.0
    // Deltas: keep Dao5.0 (latest) → delete Dao4.0
    expect(candidates.map((c) => c.key)).toEqual([
      'dao-browser-0.4.0-mac-arm64.dmg',
      'dao-browser-0.3.0-mac-arm64.dmg',
      'Dao4.0-3.0.delta',
    ]);
    expect(candidates.map((c) => c.version)).toEqual([
      '0.4.0',
      '0.3.0',
      '4.0',
    ]);
  });

  it('keeps DMGs and deltas independently — delta build numbers do not pollute DMG keep set', () => {
    // DMG versions: 1.0.44, 1.0.45, 1.0.46 (keep 2 → protect 1.0.46 + 1.0.45)
    // Delta build numbers: 13.0, 14.0, 15.0 (keep 2 → protect 15.0 + 14.0)
    const candidates = getCleanupCandidates([
      {key: 'dao-browser-1.0.44-mac-arm64.dmg', size: 100},
      {key: 'dao-browser-1.0.45-mac-arm64.dmg', size: 100},
      {key: 'dao-browser-1.0.46-mac-arm64.dmg', size: 100},
      {key: 'Dao13.0-12.0.delta', size: 10},
      {key: 'Dao14.0-13.0.delta', size: 10},
      {key: 'Dao15.0-14.0.delta', size: 10},
    ], 2);

    expect(candidates.map((c) => c.key)).toEqual([
      'dao-browser-1.0.44-mac-arm64.dmg',
      'Dao13.0-12.0.delta',
    ]);
  });

  it('associates deleted DMGs with deltas by release order', () => {
    const candidates = getCleanupCandidates([
      {key: 'dao-browser-1.0.44-mac-arm64.dmg', size: 100},
      {key: 'dao-browser-1.0.45-mac-arm64.dmg', size: 100},
      {key: 'dao-browser-1.0.46-mac-arm64.dmg', size: 100},
      {key: 'Dao13.0-12.0.delta', size: 10},
      {key: 'Dao14.0-13.0.delta', size: 10},
      {key: 'Dao15.0-14.0.delta', size: 10},
    ], 1);

    expect(candidates.map((c) => c.key)).toEqual([
      'dao-browser-1.0.45-mac-arm64.dmg',
      'dao-browser-1.0.44-mac-arm64.dmg',
      'Dao14.0-13.0.delta',
      'Dao13.0-12.0.delta',
    ]);

    const associations = getAssociatedDeltaIndexes(candidates);
    expect((associations.get(0) || []).map((i) => candidates[i].key)).toEqual([
      'Dao14.0-13.0.delta',
    ]);
    expect((associations.get(1) || []).map((i) => candidates[i].key)).toEqual([
      'Dao13.0-12.0.delta',
    ]);
  });

  it('hides automatically associated deltas from the interactive picker', () => {
    const candidates = getCleanupCandidates([
      {key: 'dao-browser-1.0.44-mac-arm64.dmg', size: 100},
      {key: 'dao-browser-1.0.45-mac-arm64.dmg', size: 100},
      {key: 'dao-browser-1.0.46-mac-arm64.dmg', size: 100},
      {key: 'Dao13.0-12.0.delta', size: 10},
      {key: 'Dao14.0-13.0.delta', size: 10},
      {key: 'Dao15.0-14.0.delta', size: 10},
    ], 1);

    expect(getSelectableCleanupIndexes(candidates).map((i) => candidates[i].key))
        .toEqual([
          'dao-browser-1.0.45-mac-arm64.dmg',
          'dao-browser-1.0.44-mac-arm64.dmg',
        ]);
  });

  it('does not expose a non-interactive cleanup delete option', () => {
    const cleanupCommand = uploadCommand.commands.find(
        (command) => command.name() === 'cleanup');

    expect(cleanupCommand).toBeDefined();
    expect(cleanupCommand!.helpInformation()).not.toContain('--yes');
  });
});
