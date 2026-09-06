# Dev-server container for the INVOKE console. Web Bluetooth runs in the
# browser, not here — this just serves the Vite app over HTTPS so an Android
# phone on the same LAN can open it.
FROM node:22-alpine

WORKDIR /app
COPY package.json package-lock.json* ./
RUN npm install

COPY . .

EXPOSE 5173
CMD ["npm", "run", "dev", "--", "--host"]
