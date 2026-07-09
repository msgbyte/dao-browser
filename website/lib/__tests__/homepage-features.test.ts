import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { describe, expect, it } from 'vitest';

const featureGridSource = readFileSync(
  resolve(dirname(fileURLToPath(import.meta.url)), '../../components/FeatureGrid.tsx'),
  'utf8',
);

describe('homepage feature grid', () => {
  it('highlights Force Dark Mode as a feature', () => {
    expect(featureGridSource).toContain('Force Dark Mode');
    expect(featureGridSource).toContain('Chromium Auto Dark Mode');
    expect(featureGridSource).toContain("icon: 'moon'");
  });

  it('includes MV2 extension compatibility as a feature', () => {
    expect(featureGridSource).toContain('MV2 Extension Compatibility');
    expect(featureGridSource).toContain('Manifest V2 extensions');
    expect(featureGridSource).toContain("icon: 'shield-check'");
  });
});
