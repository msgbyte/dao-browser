import type { Metadata } from 'next';
import { DownloadRedirect } from './DownloadRedirect';

/**
 * Internal page rendered when a browser (Accept: text/html) hits `/download`.
 * The `/download` Route Handler rewrites HTML requests here; non-HTML
 * requests are 302-redirected straight to the platform download URL.
 *
 * The real download URLs and the current product version live in
 * `/public/info.json`. To publish a new release, just update that JSON;
 * the website does not need to be rebuilt.
 */

export const metadata: Metadata = {
  title: 'Download Dao Browser',
  // Prevent search engines from indexing the redirect itself.
  robots: { index: false, follow: false },
};

export default function DownloadPage() {
  return <DownloadRedirect />;
}
