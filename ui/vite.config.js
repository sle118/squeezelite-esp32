import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// base: './' so the same build can be served from the device root or from a
// GitHub Pages sub-path (https://user.github.io/squeezelite-esp32/)
export default defineConfig({
  plugins: [svelte()],
  base: './',
  build: {
    target: 'es2020',
    assetsInlineLimit: 4096,
    // stable file names: the device's shell page links app.js / app.css directly
    rollupOptions: {
      output: {
        entryFileNames: 'app.js',
        chunkFileNames: 'chunk-[name].js',
        assetFileNames: (info) =>
          (info.name ?? '').endsWith('.css') ? 'app.css' : 'assets/[name][extname]',
      },
    },
  },
});
