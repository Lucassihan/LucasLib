import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';

// Two modes:
//  - `npm run dev`   → normal Vite dev server on :5180
//  - `npm run build` → ONE self-contained dist/index.html you can double-click
//    (all JS/CSS/images inlined, relative base so it works from file://).
export default defineConfig({
  base: './',
  plugins: [svelte(), viteSingleFile()],
  build: {
    assetsInlineLimit: 100000000, // inline every asset (images) as data URLs
    chunkSizeWarningLimit: 100000,
    cssCodeSplit: false
  },
  resolve: {
    alias: {
      $lib: '/src/lib'
    }
  },
  server: {
    port: 5180,
    strictPort: false
  }
});
