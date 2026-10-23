import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

// Development: the browser only reaches OCTAVE through this dev server.
// `/ws` is proxied to the service's WebSocket and the Origin header is
// dropped on the upgrade, so OCTAVE treats the dev page like a script
// (its Origin rule otherwise admits only the page it serves itself).
// Production: `octave --service --web-root frontends/glass/dist` serves the
// built page and the client connects to ws://<location.host>/ directly.
const port = process.env.OCTAVE_PORT ?? '9876'
const host = process.env.OCTAVE_HOST ?? '127.0.0.1'

export default defineConfig({
  plugins: [svelte()],
  base: './',
  server: {
    host: '127.0.0.1',
    proxy: {
      '/ws': {
        target: `ws://${host}:${port}`,
        ws: true,
        rewrite: () => '/',
        configure(proxy) {
          proxy.on('proxyReqWs', (proxyReq) => {
            proxyReq.removeHeader('origin')
          })
        },
      },
    },
  },
  build: { target: 'es2022' },
})
