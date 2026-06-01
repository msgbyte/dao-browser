import {describe, expect, it} from 'vitest';

import {
  getCleanupCandidates,
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
      {key: 'dao-browser-0.4.0-0.3.0.delta', size: 5},
      {key: 'dao-browser-0.5.0-mac-arm64.dmg', size: 30},
      {key: 'appcast.xml', size: 40},
      {key: 'notes.txt', size: 50},
    ], 1);

    expect(candidates.map((c) => c.key)).toEqual([
      'dao-browser-0.4.0-mac-arm64.dmg',
      'dao-browser-0.4.0-0.3.0.delta',
      'dao-browser-0.3.0-mac-arm64.dmg',
    ]);
    expect(candidates.map((c) => c.version)).toEqual([
      '0.4.0',
      '0.4.0',
      '0.3.0',
    ]);
  });

  it('does not expose a non-interactive cleanup delete option', () => {
    const cleanupCommand = uploadCommand.commands.find(
        (command) => command.name() === 'cleanup');

    expect(cleanupCommand).toBeDefined();
    expect(cleanupCommand!.helpInformation()).not.toContain('--yes');
  });
});
