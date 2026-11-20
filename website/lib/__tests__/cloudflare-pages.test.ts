// @vitest-environment node
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { describe, expect, it } from 'vitest';

const websiteRoot = resolve(dirname(fileURLToPath(import.meta.url)), '../..');

interface WebsiteNextConfig {
  output?: string;
  redirects?: () => Promise<Array<{
    source: string;
    destination: string;
    permanent: boolean;
  }>>;
}

async function loadNextConfig(): Promise<(phase: string) => WebsiteNextConfig> {
  const mod = (await import(
    pathToFileURL(resolve(websiteRoot, 'next.config.mjs')).href
  )) as { default: (phase: string) => WebsiteNextConfig };
  return mod.default;
}

function readPublicFile(name: string): string {
  return readFileSync(resolve(websiteRoot, 'public', name), 'utf8');
}

describe('Cloudflare Pages static deployment configuration', () => {
  it('configures Next.js static export and dev-only parity redirects', async () => {
    const nextConfig = await loadNextConfig();
    const prodConfig = nextConfig('phase-production-build');
    expect(prodConfig.output).toBe('export');
    expect(prodConfig.redirects).toBeUndefined();

    const devConfig = nextConfig('phase-development-server');
    expect(devConfig.output).toBe('export');
    expect(devConfig.redirects).toBeTypeOf('function');
    const redirects = await devConfig.redirects!();
    expect(redirects).toEqual([
      {
        source: '/download',
        destination: '/download-browser',
        permanent: false,
      },
      {
        source: '/changelog/zh-CN',
        destination: '/changelog',
        permanent: true,
      },
    ]);
  });

  it('redirects /download and /changelog/zh-CN via public/_redirects', () => {
    const lines = readPublicFile('_redirects')
      .split(/\r?\n/u)
      .map((line) => line.trim())
      .filter((line) => line && !line.startsWith('#'));

    expect(lines).toContain('/download /download-browser 302');
    expect(lines).toContain('/download/ /download-browser 302');
    expect(lines).toContain('/changelog/zh-CN /changelog 308');
    expect(lines).toContain('/changelog/zh-CN/ /changelog 308');
  });

  it('sets cache-control and opengraph content-type headers via public/_headers', () => {
    const headers = readPublicFile('_headers');
    const windowsBlock = headers.match(
      /^\/windows-update\.json\r?\n((?:[ \t]+\S.*\r?\n?)+)/mu,
    )?.[1];
    const ogBlock = headers.match(
      /^\/opengraph-image\r?\n((?:[ \t]+\S.*\r?\n?)+)/mu,
    )?.[1];

    expect(windowsBlock).toBeDefined();
    expect(windowsBlock).toContain('Cache-Control: no-store');
    expect(windowsBlock).toContain('CDN-Cache-Control: no-store');
    expect(windowsBlock).toContain('Cloudflare-CDN-Cache-Control: no-store');

    expect(ogBlock).toBeDefined();
    expect(ogBlock).toContain('Content-Type: image/png');
  });
});
