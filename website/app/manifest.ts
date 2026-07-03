import type { MetadataRoute } from 'next';
import { DEFAULT_DESCRIPTION, SITE_NAME } from '@/lib/seo';

export default function manifest(): MetadataRoute.Manifest {
  return {
    name: SITE_NAME,
    short_name: 'Dao',
    description: DEFAULT_DESCRIPTION,
    start_url: '/',
    scope: '/',
    display: 'standalone',
    background_color: '#e7eef5',
    theme_color: '#4678be',
    categories: ['productivity', 'utilities'],
    icons: [
      {
        src: '/icon-light.png',
        sizes: '32x32',
        type: 'image/png',
      },
      {
        src: '/apple-icon-light.png',
        sizes: '128x128',
        type: 'image/png',
      },
    ],
  };
}
