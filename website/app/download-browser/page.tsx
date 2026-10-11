import type { Metadata } from 'next';
import { DownloadRedirect } from './DownloadRedirect';
import messages from '@/locales/en/download.json';

/**
 * Internal page rendered when a visitor hits `/download`.
 * The `/download` Route Handler redirects all requests here.
 * Only macOS automatically downloads; other devices choose a download link.
 *
 * The real download URLs and the current product version live in
 * `/public/info.json`. To publish a new release, just update that JSON;
 * the website does not need to be rebuilt.
 */

export const metadata: Metadata = {
  title: messages.title,
  // Prevent search engines from indexing the redirect itself.
  robots: { index: false, follow: false },
};

export default function DownloadPage() {
  return <DownloadRedirect />;
}
