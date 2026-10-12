import { defineConfig } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

export default defineConfig({
  plugins: [viteSingleFile()],
  server: {
    port: 3000,
    open: false
  },
  build: {
    target: 'esnext'
  },
  test: {
    include: ['tests/**/*.spec.ts', 'tests/**/*.test.ts']
  }
} as any);
