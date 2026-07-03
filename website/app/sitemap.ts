import type { MetadataRoute } from 'next';
import { PRODUCT_RELEASED_AT, SITE_URL } from '@/lib/version';

const lastModified = new Date(`${PRODUCT_RELEASED_AT}T00:00:00.000Z`);

export default function sitemap(): MetadataRoute.Sitemap {
  return [
    {
      url: SITE_URL,
      lastModified,
      changeFrequency: 'weekly',
      priority: 1,
    },
    {
      url: `${SITE_URL}/agent`,
      lastModified,
      changeFrequency: 'monthly',
      priority: 0.9,
    },
    {
      url: `${SITE_URL}/history`,
      lastModified,
      changeFrequency: 'weekly',
      priority: 0.7,
    },
  ];
}
