import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import basicSsl from '@vitejs/plugin-basic-ssl';

// Web Bluetooth only runs on a secure context: https:// or localhost. On the
// desktop, http://localhost is fine and you can drop basicSsl. To reach the
// dev server from an Android phone you need https:// — basicSsl serves a
// self-signed cert (Chrome shows a one-time "Not secure" warning to accept).
export default defineConfig({
  plugins: [react(), basicSsl()],
  server: {
    host: true, // listen on 0.0.0.0 so the phone on the same LAN can reach it
    port: 5173,
  },
});
