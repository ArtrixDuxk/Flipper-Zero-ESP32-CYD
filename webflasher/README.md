# Flipper CYD Web Flasher

Web Serial installer for the `esp32_cyd_nm_rf_hat` firmware target.

## Requirements

- Node.js 20 or newer
- A completed CYD firmware build in `../build_cyd`
- Chrome or Edge for Web Serial

## Update the bundled firmware

From this directory:

```bash
npm run sync-firmware -- 1.4.3
```

The sync script copies the bootloader, partition table and application binary,
then regenerates `public/firmware/manifest.json` with their sizes, offsets and
SHA-256 checksums.

## Run locally

```bash
npm install
npm run dev
```

Open `http://localhost:3000`. Web Serial works on localhost and secure HTTPS
origins. Close qFlipper and any serial monitor before selecting the CH340 port.

## Validate a production build

```bash
npm run lint
npm run build
npm run build:cloudflare
```
