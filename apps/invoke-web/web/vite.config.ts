import { fileURLToPath } from 'node:url';
import basicSsl from '@vitejs/plugin-basic-ssl';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

const srcDir = fileURLToPath(new URL('./src', import.meta.url));

// Web Bluetooth needs a secure context. http://localhost is fine on the
// desktop; to open the app from an Android phone on the LAN you need https://,
// so basicSsl serves a self-signed cert (accept the one-time Chrome warning).
const apiTarget = process.env.VITE_API_PROXY ?? 'http://localhost:8787';

export default defineConfig({
  plugins: [react(), basicSsl()],
  resolve: {
    alias: { '@': srcDir },
  },
  server: {
    host: true,
    port: 5173,
    proxy: {
      '/api': { target: apiTarget, changeOrigin: true },
    },
  },
});
