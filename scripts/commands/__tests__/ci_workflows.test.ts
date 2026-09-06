import {spawnSync} from 'node:child_process';
import {existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

function read(relativePath: string): string {
  return readFileSync(path.join(process.cwd(), relativePath), 'utf-8');
}

describe('CI workflows', () => {
  it('requires Android signing secrets and cleans up the restored key', () => {
    const workflow = read('.github/workflows/build-android-apk.yml');
    const releaseStep = workflow.match(
        /      - name: Build release APK\n([\s\S]*?)(?=\n      - name:)/u)?.[1];
    expect(releaseStep).toContain("if: github.event_name != 'pull_request'");
    const script = releaseStep!.split('run: |\n')[1]
        .replace(/^          /gmu, '');
    const directory = mkdtempSync(path.join(tmpdir(), 'dao-android-signing-'));
    const signingEnv = {
      ANDROID_KEYSTORE_BASE64: Buffer.from('test-keystore').toString('base64'),
      ANDROID_KEYSTORE_PASSWORD: 'test-password',
      ANDROID_KEY_PASSWORD: 'test-password',
    };
    const run = (env: NodeJS.ProcessEnv) => spawnSync(
        'bash', ['-e', '-u', '-o', 'pipefail', '-c', script], {
          cwd: directory,
          env: {PATH: process.env.PATH, ...env},
          encoding: 'utf-8',
        });

    try {
      writeFileSync(path.join(directory, 'gradlew'), [
        '#!/bin/bash',
        'test "$(cat dao-release.jks)" = "test-keystore" || exit 2',
        'printf "%s\\n" "$@"',
        'exit "$GRADLE_EXIT_CODE"',
      ].join('\n'), {mode: 0o700});

      for (const name of Object.keys(signingEnv)) {
        const result = run({...signingEnv, [name]: ''});
        expect(result.status).toBe(1);
        expect(result.stdout + result.stderr).toContain(name);
        expect(existsSync(path.join(directory, 'dao-release.jks'))).toBe(false);
      }

      for (const exitCode of [0, 1]) {
        const result = run({...signingEnv, GRADLE_EXIT_CODE: String(exitCode)});
        expect(result.status).toBe(exitCode);
        expect(result.stdout).toContain(':app:assembleRelease');
        expect(existsSync(path.join(directory, 'dao-release.jks'))).toBe(false);
      }
    } finally {
      rmSync(directory, {recursive: true, force: true});
    }
  });

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
