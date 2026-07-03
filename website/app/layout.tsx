import type { Metadata } from 'next';
import Script from 'next/script';
import './globals.css';
import { SITE_URL } from '@/lib/version';
import { TIANJI_WEBSITE_ID } from '@/lib/analytics';
import {
  DEFAULT_DESCRIPTION,
  DEFAULT_TITLE,
  ORGANIZATION_JSON_LD,
  SOFTWARE_APPLICATION_JSON_LD,
  WEBSITE_JSON_LD,
  createMetadata,
  jsonLdScriptProps,
} from '@/lib/seo';

export const metadata: Metadata = {
  ...createMetadata({
    title: DEFAULT_TITLE,
    description: DEFAULT_DESCRIPTION,
    path: '/',
  }),
  metadataBase: new URL(SITE_URL),
  manifest: '/manifest.webmanifest',
  icons: {
    // Two-theme favicons. Browsers that support `media` pick the one matching
    // the system color-scheme; older browsers fall back to the first entry.
    icon: [
      {
        url: '/icon-light.png',
        type: 'image/png',
        sizes: '32x32',
        media: '(prefers-color-scheme: light)',
      },
      {
        url: '/icon-dark.png',
        type: 'image/png',
        sizes: '32x32',
        media: '(prefers-color-scheme: dark)',
      },
      // Default fallback for browsers without media-aware icon support.
      { url: '/icon-light.png', type: 'image/png', sizes: '32x32' },
    ],
    apple: [
      {
        url: '/apple-icon-light.png',
        sizes: '128x128',
        media: '(prefers-color-scheme: light)',
      },
      {
        url: '/apple-icon-dark.png',
        sizes: '128x128',
        media: '(prefers-color-scheme: dark)',
      },
    ],
  },
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>
        {children}
        <script
          {...jsonLdScriptProps([
            ORGANIZATION_JSON_LD,
            WEBSITE_JSON_LD,
            SOFTWARE_APPLICATION_JSON_LD,
          ])}
        />
        <Script
          src="https://app.tianji.dev/tracker.js"
          data-website-id={TIANJI_WEBSITE_ID}
          strategy="afterInteractive"
        />
      </body>
    </html>
  );
}
