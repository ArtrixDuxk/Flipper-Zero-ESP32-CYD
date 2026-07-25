# Flipper CYD Web Flasher

Web Serial installer for the `esp32_cyd_nm_rf_hat` firmware target. The site
flashes the ESP32 firmware only. A separate ZIP download contains the starter
resources that users copy to the microSD card manually.

## Requirements

- Node.js 20 or newer
- A completed CYD firmware build in `../build_cyd`
- Chrome or Edge for Web Serial
- A microSD card that can be formatted as FAT32

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
generates:

- `public/downloads/flipper-cyd-sd-card.zip`, ready to extract to the card root
- `public/sdcard-manifest.json`, with the file list and checksums used by the site

`npm run build` runs this sync automatically before creating the static site.

To prepare a card:

1. Format the microSD card as FAT32.
2. Download and extract `flipper-cyd-sd-card.zip`.
3. Copy everything **inside** the extracted folder to the microSD card root.
4. Safely eject the card and insert it into the powered-off CYD.

The ZIP itself must not be copied to the card. Directories such as `subghz`,
`nfc`, and `wifi` must appear directly at the card root.

## Run locally

```bash
npm install
npm run dev
```

Open `http://localhost:3000`. Web Serial works on localhost and secure HTTPS
origins. Close qFlipper and any serial monitor before selecting the CH340 port.
The SD package can be downloaded from the tutorial directly below the flasher.

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
