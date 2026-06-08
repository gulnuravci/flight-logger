import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import { VitePWA } from 'vite-plugin-pwa'

export default defineConfig({
  base: '/flight-logger/',
  plugins: [
    react(),
    tailwindcss(),
    VitePWA({
      registerType: 'autoUpdate',
      manifest: {
        name: 'Flight Logger',
        short_name: 'FlightLog',
        description: 'Review your Pico flight data',
        start_url: '/flight-logger/',
        display: 'standalone',
        background_color: '#020617',
        theme_color: '#020617',
        icons: [
          {
            src: '/flight-logger/favicon.svg',
            sizes: 'any',
            type: 'image/svg+xml',
            purpose: 'any maskable',
          },
        ],
      },
      workbox: {
        // Cache all built assets so the app shell works fully offline
        globPatterns: ['**/*.{js,css,html,svg,png,ico,woff2}'],
        navigateFallback: '/flight-logger/index.html',
        runtimeCaching: [
          {
            // Never cache Pico API calls — must reach the real device
            urlPattern: /^http:\/\/192\.168\.4\.1\//,
            handler: 'NetworkOnly',
          },
        ],
      },
    }),
  ],
})
