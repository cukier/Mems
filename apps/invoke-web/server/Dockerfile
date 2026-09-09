# Local entities API. better-sqlite3 ships prebuilt binaries for common
# platforms, but keep a toolchain around in case it has to compile from source.
FROM node:22-bookworm-slim

RUN apt-get update \
  && apt-get install -y --no-install-recommends python3 make g++ ca-certificates \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY package.json package-lock.json* ./
RUN npm install

COPY . .

EXPOSE 8787
CMD ["npm", "run", "dev"]
