import { fileURLToPath } from 'node:url';
import { dirname } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));

/** @type {import('next').NextConfig} */
const nextConfig = {
  images: { unoptimized: true },
  reactStrictMode: true,
  // The repo root has its own lockfile (for the Chromium build toolchain). Pin
  // Next.js's workspace root to the website directory to silence the warning.
  outputFileTracingRoot: __dirname,
};

export default nextConfig;
