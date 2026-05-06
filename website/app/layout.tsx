import type { Metadata } from 'next';
import './globals.css';
import { SITE_URL } from '@/lib/version';

export const metadata: Metadata = {
  metadataBase: new URL(SITE_URL),
  title: 'Dao Browser — An opinionated browser.',
  description:
    'An opinionated browser, built on Chromium. Vertical tabs, soft corners, content first.',
  openGraph: {
    title: 'Dao Browser',
    description: 'An opinionated browser, built on Chromium.',
    url: SITE_URL,
    siteName: 'Dao Browser',
    // Image is generated at build time by app/opengraph-image.tsx
    // and auto-injected by Next.js, no need to declare it here.
    type: 'website',
  },
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
      <body>{children}</body>
    </html>
  );
}
