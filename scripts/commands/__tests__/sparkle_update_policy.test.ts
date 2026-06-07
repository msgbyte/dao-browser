import {readFileSync} from 'node:fs';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

describe('Sparkle update policy', () => {
  it('checks automatically every 24 hours', () => {
    const patch = readFileSync(
        path.join(process.cwd(), 'src/patches/chrome/app/app-Info.plist.patch'),
        'utf-8');

    expect(patch).toContain('<key>SUScheduledCheckInterval</key>');
    expect(patch).toContain('<integer>86400</integer>');
    expect(patch).not.toContain('<integer>3600</integer>');
  });

  it('performs a background update check on startup', () => {
    const sparkleWrapper = readFileSync(
        path.join(
            process.cwd(),
            'src/dao/browser/updater/dao_sparkle_updater_mac.mm',
        ),
        'utf-8');

    expect(sparkleWrapper).toContain('[controller.updater checkForUpdatesInBackground]');
  });
});
