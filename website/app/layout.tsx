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
    // Forced-dark site (plan 1): always serve the dark favicons.
    icon: [{ url: '/icon-dark.png', type: 'image/png', sizes: '32x32' }],
    apple: [{ url: '/apple-icon-dark.png', sizes: '128x128' }],
  },
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en" style={{ colorScheme: 'dark' }}>
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
