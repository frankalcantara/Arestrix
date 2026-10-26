import { defineConfig } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';
import path from 'path';

export default defineConfig({
  plugins: [viteSingleFile()],
  resolve: {
    alias: {
      'i18next': path.resolve(__dirname, 'third_party/i18next/src/index.js')
    }
  },
  server: {
    port: 3000,
    open: false
  },
  build: {
    target: 'esnext'
  },
  test: {
    setupFiles: ['./tests/setup.ts'],
    include: ['tests/**/*.spec.ts', 'tests/**/*.test.ts']
  }
} as any);
