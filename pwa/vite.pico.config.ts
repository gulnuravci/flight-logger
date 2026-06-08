// Build config for files served directly from the Pico's SD card.
// base: '/' because the app is hosted at http://192.168.4.1/ (no subpath).
// No service worker needed — the Pico is the server.
//
// All output filenames use FAT 8.3 format (≤8 char stem, ≤3 char extension)
// because FatFS is compiled without LFN support on the RP2350.
//   index.html → renamed to index.htm by the build:pico script
//   assets/index-HASH.js  → assets/main.js
//   assets/index-HASH.css → assets/main.css
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  base: '/',
  plugins: [react(), tailwindcss()],
  build: {
    outDir: 'dist-pico',
    emptyOutDir: true,
    rollupOptions: {
      output: {
        entryFileNames: 'main.js',
        chunkFileNames: 'main.js',
        assetFileNames: 'main[extname]',  // main.css, etc.
      },
    },
  },
})
