# Vite dev server for the INVOKE teacher app. Web Bluetooth runs in the
# browser, not here — this container just serves the app over HTTPS so a phone
# on the same LAN can open it.
FROM node:22-alpine

WORKDIR /app
COPY package.json package-lock.json* ./
RUN npm install

COPY . .

EXPOSE 5173
CMD ["npm", "run", "dev", "--", "--host"]
