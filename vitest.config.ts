import { defineConfig } from 'vitest/config';
import { fileURLToPath } from 'node:url';

export default defineConfig({
  resolve: { alias: { '@': fileURLToPath(new URL('./website', import.meta.url)) } },
  oxc: { jsx: { runtime: 'automatic' } },
  test: {
    environment: 'jsdom',
    include: [
      'src/dao/**/__tests__/**/*.test.ts',
      'website/**/__tests__/**/*.test.ts',
      'scripts/**/__tests__/**/*.test.ts',
    ],
    exclude: ['node_modules', 'engine', 'website/node_modules', 'website/.next'],
    globals: false,
    restoreMocks: true,
  },
});
