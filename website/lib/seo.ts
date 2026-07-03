import type { Metadata } from 'next';
import {
  CHROMIUM_VERSION,
  DOWNLOAD_URL_MAC_ARM64,
  GITHUB_URL,
  PRODUCT_RELEASED_AT,
  PRODUCT_VERSION,
  SITE_URL,
} from './version';

export type JsonLdValue =
  | string
  | number
  | boolean
  | null
  | JsonLd
  | JsonLdValue[];

export interface JsonLd {
  [key: string]: JsonLdValue;
}

export const SITE_NAME = 'Dao Browser';
export const DEFAULT_TITLE =
  'Dao Browser - AI Browser with Vertical Tabs, Built on Chromium';
export const DEFAULT_DESCRIPTION =
  'Dao Browser is an open-source Chromium browser for macOS with vertical tabs, a browser-native AI agent, command bar, split view, and content-first design.';

export const SEO_KEYWORDS = [
  'Dao Browser',
  'AI browser',
  'Chromium browser',
  'open source browser',
  'vertical tabs browser',
  'browser AI agent',
  'Arc-style browser',
  'macOS browser',
];

export const DEFAULT_OG_IMAGE = {
  url: `${SITE_URL}/opengraph-image`,
  width: 1200,
  height: 630,
  alt: 'Dao Browser - AI browser with vertical tabs, built on Chromium',
};

export function absoluteUrl(path = '/'): string {
  return new URL(path, SITE_URL).toString();
}

interface CreateMetadataOptions {
  title: string;
  description: string;
  path?: string;
  keywords?: string[];
  imageAlt?: string;
}

export function createMetadata({
  title,
  description,
  path = '/',
  keywords = [],
  imageAlt = DEFAULT_OG_IMAGE.alt,
}: CreateMetadataOptions): Metadata {
  const url = absoluteUrl(path);

  return {
    title,
    description,
    applicationName: SITE_NAME,
    authors: [{ name: 'msgbyte', url: 'https://github.com/msgbyte' }],
    creator: 'msgbyte',
    publisher: SITE_NAME,
    category: 'technology',
    keywords: [...SEO_KEYWORDS, ...keywords],
    alternates: {
      canonical: url,
    },
    robots: {
      index: true,
      follow: true,
      googleBot: {
        index: true,
        follow: true,
        'max-image-preview': 'large',
        'max-snippet': -1,
        'max-video-preview': -1,
      },
    },
    openGraph: {
      title,
      description,
      url,
      siteName: SITE_NAME,
      type: 'website',
      images: [{ ...DEFAULT_OG_IMAGE, alt: imageAlt }],
    },
    twitter: {
      card: 'summary_large_image',
      title,
      description,
      images: [DEFAULT_OG_IMAGE.url],
    },
  };
}

const organizationRef: JsonLd = {
  '@id': `${SITE_URL}/#organization`,
};

const softwareRef: JsonLd = {
  '@id': `${SITE_URL}/#software`,
};

export const ORGANIZATION_JSON_LD: JsonLd = {
  '@context': 'https://schema.org',
  '@id': `${SITE_URL}/#organization`,
  '@type': 'Organization',
  name: SITE_NAME,
  url: SITE_URL,
  logo: {
    '@type': 'ImageObject',
    url: absoluteUrl('/dao-logo-light.png'),
    width: 256,
    height: 256,
  },
  sameAs: [GITHUB_URL],
};

export const WEBSITE_JSON_LD: JsonLd = {
  '@context': 'https://schema.org',
  '@id': `${SITE_URL}/#website`,
  '@type': 'WebSite',
  name: SITE_NAME,
  url: SITE_URL,
  description: DEFAULT_DESCRIPTION,
  inLanguage: 'en-US',
  publisher: organizationRef,
};

export const SOFTWARE_APPLICATION_JSON_LD: JsonLd = {
  '@context': 'https://schema.org',
  '@id': `${SITE_URL}/#software`,
  '@type': 'SoftwareApplication',
  name: SITE_NAME,
  url: SITE_URL,
  description: DEFAULT_DESCRIPTION,
  applicationCategory: 'WebBrowser',
  operatingSystem: 'macOS',
  softwareVersion: PRODUCT_VERSION,
  datePublished: PRODUCT_RELEASED_AT,
  browserRequirements: `Built on Chromium ${CHROMIUM_VERSION}`,
  downloadUrl: DOWNLOAD_URL_MAC_ARM64,
  codeRepository: GITHUB_URL,
  isAccessibleForFree: true,
  publisher: organizationRef,
  offers: {
    '@type': 'Offer',
    price: '0',
    priceCurrency: 'USD',
  },
  featureList: [
    'Vertical tabs',
    'Browser-native AI agent',
    'Command bar',
    'Split view',
    'Picture-in-picture',
    'Content-first browser chrome',
    'OpenAI-compatible model configuration',
  ],
};

interface WebPageJsonLdOptions {
  title: string;
  description: string;
  path: string;
  type?: 'WebPage' | 'AboutPage' | 'CollectionPage';
}

export function buildWebPageJsonLd({
  title,
  description,
  path,
  type = 'WebPage',
}: WebPageJsonLdOptions): JsonLd {
  const url = absoluteUrl(path);

  return {
    '@context': 'https://schema.org',
    '@id': `${url}#webpage`,
    '@type': type,
    name: title,
    description,
    url,
    inLanguage: 'en-US',
    isPartOf: {
      '@id': `${SITE_URL}/#website`,
    },
    about: softwareRef,
    publisher: organizationRef,
  };
}

export function jsonLdScriptProps(data: JsonLd | JsonLd[]) {
  return {
    type: 'application/ld+json',
    dangerouslySetInnerHTML: {
      __html: JSON.stringify(data).replace(/</g, '\\u003c'),
    },
  };
}
