import { defineConfig } from 'vitest/config';

export default defineConfig({
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
