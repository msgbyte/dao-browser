import {existsSync, readFileSync} from 'node:fs';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

function read(relativePath: string): string {
  return readFileSync(path.join(process.cwd(), relativePath), 'utf-8');
}

describe('CI workflows', () => {
  it('exposes a non-Chromium TypeScript check', () => {
    const packageJson = JSON.parse(read('package.json')) as {
      scripts?: Record<string, string>;
    };

    expect(packageJson.scripts?.typecheck).toBe('tsc --noEmit');
  });

  it('runs every lightweight root check for pull requests', () => {
    const workflowPath = '.github/workflows/fast-checks.yml';
    expect(existsSync(path.join(process.cwd(), workflowPath))).toBe(true);

    const workflow = read(workflowPath);
    expect(workflow).toContain('pull_request:');
    expect(workflow).toContain('run: npm ci');
    expect(workflow).toContain('run: npm run test:webui');
    expect(workflow).toContain('run: npm run lint:lit');
    expect(workflow).toContain('run: npm run vendor:check');
    expect(workflow).toContain('run: npm run typecheck');
    expect(workflow).toContain('run: npm run docs:check');
    expect(workflow).not.toMatch(/npm run (?:rebuild|build(?::debug)?|test:build)/u);
  });

  it('checks the website before deploying with a pinned Vercel CLI', () => {
    const workflow = read('.github/workflows/deploy-website.yml');
    const websitePackageJson = JSON.parse(read('website/package.json')) as {
      devDependencies?: Record<string, string>;
    };

    expect(workflow).toMatch(/jobs:\n  check:/u);
    expect(workflow).toMatch(/\n  deploy:\n/u);
    expect(workflow).toContain('needs: check');
    expect(workflow).toContain('run: npm run check');
    expect(workflow).toContain('run: npm run lint');
    expect(workflow).toContain('run: npm run build');
    expect(workflow).toContain('VERCEL_CLI_VERSION: 58.5.1');
    expect(workflow).toContain('vercel@"${VERCEL_CLI_VERSION}"');
    expect(workflow).not.toContain('vercel@latest');
    expect(websitePackageJson.devDependencies?.vitest).toBeDefined();
  });

  it('archives each pushed release tag from R2 to GitHub Releases', () => {
    const workflowPath = '.github/workflows/publish-github-release.yml';
    expect(existsSync(path.join(process.cwd(), workflowPath))).toBe(true);

    const workflow = read(workflowPath);
    expect(workflow).toContain("tags:\n      - 'v*'");
    expect(workflow).toContain('contents: write');
    expect(workflow).toContain('run: npm ci');
    expect(workflow).toContain(
        'run: npm run release:github -- "$GITHUB_REF_NAME"');
    expect(workflow).toContain('GH_TOKEN: ${{ github.token }}');
  });
});
