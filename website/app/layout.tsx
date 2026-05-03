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
  // icons are auto-injected by Next.js from app/icon.png and app/apple-icon.png
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
