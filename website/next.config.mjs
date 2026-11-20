import { fileURLToPath } from 'node:url';
import { dirname } from 'node:path';
import nextConstants from 'next/constants.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const { PHASE_DEVELOPMENT_SERVER } = nextConstants;

/** @type {(phase: string) => import('next').NextConfig} */
export default function nextConfig(phase) {
  return {
    output: 'export',
    images: { unoptimized: true },
    reactStrictMode: true,
    // The repo root has its own lockfile (for the Chromium build toolchain). Pin
    // Next.js's workspace root to the website directory to silence the warning.
    outputFileTracingRoot: __dirname,
    // Production redirects live in public/_redirects for Cloudflare Pages.
    // Mirror them in `next dev` so local development matches production.
    ...(phase === PHASE_DEVELOPMENT_SERVER
      ? {
          async redirects() {
            return [
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
            ];
          },
        }
      : {}),
  };
}
