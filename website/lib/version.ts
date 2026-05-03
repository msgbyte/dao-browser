import { z } from 'zod';
import daoJson from '../../dao.json' assert { type: 'json' };

const DaoConfigSchema = z.object({
  version: z.object({
    version: z.string().min(1, 'must be non-empty'), // Chromium version, e.g. "147.0.7727.135"
    display: z.string().min(1, 'must be non-empty'), // Product version, e.g. "0.5.0"
  }),
});

const parsed = DaoConfigSchema.safeParse(daoJson);
if (!parsed.success) {
  const issues = parsed.error.issues
    .map((i) => `${i.path.join('.') || '<root>'}: ${i.message}`)
    .join('; ');
  throw new Error(
    `Invalid dao.json shape (${issues}). ` +
      `lib/version.ts expects version.{version,display} as non-empty strings.`,
  );
}

/** Product version, e.g. "0.5.0" — sourced from dao.json `version.display`. */
export const PRODUCT_VERSION = parsed.data.version.display;

/** Chromium version, e.g. "147.0.7727.135" — sourced from dao.json `version.version`. */
export const CHROMIUM_VERSION = parsed.data.version.version;

export const GITHUB_URL = 'https://github.com/moonrailgun/dao-browser';
export const SITE_URL = 'https://dao.msgbyte.com';
export const DOWNLOAD_URL_MAC_ARM64 = `${GITHUB_URL}/releases/download/v${PRODUCT_VERSION}/dao-browser-${PRODUCT_VERSION}-mac-arm64.dmg`;
