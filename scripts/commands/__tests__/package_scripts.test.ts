import {readFileSync} from 'node:fs';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

import {assertRequiredEntitlementsPresent} from '../package.js';

describe('package scripts', () => {
  it('exposes the R2 cleanup command', () => {
    const packageJsonPath = path.join(process.cwd(), 'package.json');
    const packageJson = JSON.parse(readFileSync(packageJsonPath, 'utf-8')) as {
      scripts?: Record<string, string>;
    };

    expect(packageJson.scripts?.['upload:cleanup'])
        .toBe('tsx scripts/cli.ts upload cleanup');
  });

  it('rejects a signed helper with no embedded entitlements', () => {
    expect(() => assertRequiredEntitlementsPresent(
        'Dao Helper (Renderer).app',
        '',
        ['com.apple.security.cs.allow-jit'],
    )).toThrow(/missing signed entitlements/i);
  });

  it('accepts a signed helper with every required entitlement', () => {
    const entitlements = `<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<dict>
  <key>com.apple.security.cs.allow-jit</key>
  <true/>
  <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
  <true/>
</dict>
</plist>`;

    expect(() => assertRequiredEntitlementsPresent(
        'Dao Helper (Renderer).app',
        entitlements,
        [
          'com.apple.security.cs.allow-jit',
          'com.apple.security.cs.allow-unsigned-executable-memory',
        ],
    )).not.toThrow();
  });
});
