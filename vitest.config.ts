import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'jsdom',
    include: ['src/dao/**/__tests__/**/*.test.ts'],
    globals: false,
    restoreMocks: true,
  },
});
