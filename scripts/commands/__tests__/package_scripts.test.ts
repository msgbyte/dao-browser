import {readFileSync} from 'node:fs';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

describe('package scripts', () => {
  it('exposes the R2 cleanup command', () => {
    const packageJsonPath = path.join(process.cwd(), 'package.json');
    const packageJson = JSON.parse(readFileSync(packageJsonPath, 'utf-8')) as {
      scripts?: Record<string, string>;
    };

    expect(packageJson.scripts?.['upload:cleanup'])
        .toBe('tsx scripts/cli.ts upload cleanup');
  });
});
