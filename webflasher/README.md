# Flipper CYD Web Flasher

Web Serial installer for the `esp32_cyd_nm_rf_hat` firmware target. The same
one-click flow flashes the ESP32, reconnects to the running firmware RPC and
installs the required starter resources on the microSD card.

## Requirements

- Node.js 20 or newer
- A completed CYD firmware build in `../build_cyd`
- Chrome or Edge for Web Serial
- A FAT32-formatted microSD card inserted in the CYD

## Update the bundled firmware

From this directory:

```bash
npm run sync-firmware -- 1.4.3
```

The sync script copies the bootloader, partition table and application binary,
then regenerates `public/firmware/manifest.json` with their sizes, offsets and
SHA-256 checksums.

## Update the bundled SD starter pack

From this directory:

```bash
npm run sync-sdcard
```

The sync script collects the firmware-owned BadUSB, Infrared, LF-RFID, NFC,
Sub-GHz and U2F resources, creates the standard `/ext` folder structure and
generates both a qFlipper-compatible `/ext/Manifest` and a browser manifest
with SHA-256 and MD5 checksums. The web installer verifies downloads before
flashing and verifies every copied SD file through the CYD RPC afterward.

## Run locally

```bash
npm install
npm run dev
```

Open `http://localhost:3000`. Web Serial works on localhost and secure HTTPS
origins. Close qFlipper and any serial monitor before selecting the CH340 port.

## Create the static production build

```bash
npm run lint
npm run build
```

The deployable website is written to `out/`. Upload the contents of that
directory to any HTTPS static host, such as Cloudflare Pages, Netlify, GitHub
Pages or an ordinary web server. HTTPS (or localhost) is required by Web Serial.

For a host that serves the site from a subdirectory, set the public base path
while building. For example, GitHub Pages at `/Flipper-Zero-ESP32-CYD`:

```bash
NEXT_PUBLIC_BASE_PATH=/Flipper-Zero-ESP32-CYD npm run build
```

For root-domain hosting, run `npm run build` without `NEXT_PUBLIC_BASE_PATH`.
