import { z } from 'zod';
import infoJson from '../public/info.json' with { type: 'json' };

// Single source of truth for version + binary URLs is `public/info.json`.
// The same file is fetched at runtime by the /download route to redirect
// users to the platform-specific binary, so editing it updates both the
// static copy on the site and the redirect target without code changes.
const InfoConfigSchema = z.object({
  version: z.string().min(1, 'must be non-empty'), // Product version, e.g. "0.5.0"
  chromiumVersion: z.string().min(1, 'must be non-empty'), // Chromium version, e.g. "147.0.7727.135"
  releasedAt: z.string().min(1, 'must be non-empty'), // Release date, e.g. "2026-07-03"
  platforms: z.record(
    z.string(),
    z.object({
      label: z.string().min(1),
      url: z.string().url(),
    }),
  ),
  default: z.string().min(1),
});

const parsed = InfoConfigSchema.safeParse(infoJson);
if (!parsed.success) {
  const issues = parsed.error.issues
    .map((i) => `${i.path.join('.') || '<root>'}: ${i.message}`)
    .join('; ');
  throw new Error(
    `Invalid public/info.json shape (${issues}). ` +
      `lib/version.ts expects { version, chromiumVersion, platforms, default }.`,
  );
}

/** Product version, e.g. "0.5.0" — sourced from public/info.json `version`. */
export const PRODUCT_VERSION = parsed.data.version;

/** Chromium version, e.g. "147.0.7727.135" — sourced from public/info.json `chromiumVersion`. */
export const CHROMIUM_VERSION = parsed.data.chromiumVersion;

/** Release date, e.g. "2026-07-03" — sourced from public/info.json `releasedAt`. */
export const PRODUCT_RELEASED_AT = parsed.data.releasedAt;

export const GITHUB_URL = 'https://github.com/msgbyte/dao-browser';
export const SITE_URL = 'https://dao.msgbyte.com';

/**
 * Canonical download link used by every "Download" CTA on the site.
 *
 * Points at the local `/download` route, which reads `/info.json` at
 * runtime and redirects to the platform-appropriate release URL. Using a
 * single internal route lets us update the actual binary URL by editing
 * `public/info.json` without rebuilding the website.
 */
export const DOWNLOAD_URL = '/download';

/**
 * Direct release URL for the current Mac (Apple Silicon) build.
 * Kept as a fallback so server-side / static metadata (OG images, etc.) can
 * embed an absolute URL when a /download redirect won't work.
 */
export const DOWNLOAD_URL_MAC_ARM64 =
  parsed.data.platforms.macArm64?.url ??
  parsed.data.platforms[parsed.data.default]?.url ??
  `${GITHUB_URL}/releases/download/v${PRODUCT_VERSION}/dao-browser-${PRODUCT_VERSION}-mac-arm64.dmg`;
