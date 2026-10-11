import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

// base: './' so the built index.html works from file:// inside Electron.
export default defineConfig({
  plugins: [vue()],
  base: './',
  server: {
    host: '127.0.0.1',
    port: 5173,
    strictPort: true,
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
});
