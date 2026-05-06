import type { Metadata } from 'next';
import { DownloadRedirect } from './DownloadRedirect';

/**
 * /download — single redirect route used by every "Download" CTA on the site.
 *
 * The real download URLs and the current product version live in
 * `/public/info.json`. To publish a new release, just update that JSON;
 * the website does not need to be rebuilt.
 *
 * This route is a static page (compatible with `output: 'export'`) that
 * client-side-fetches `/info.json` and redirects via `location.replace`
 * to the platform-appropriate URL. A visible fallback lets users continue
 * manually if JS is disabled or the request fails.
 */

export const metadata: Metadata = {
  title: 'Download Dao Browser',
  // Prevent search engines from indexing the redirect itself.
  robots: { index: false, follow: false },
};

export default function DownloadPage() {
  return <DownloadRedirect />;
}
