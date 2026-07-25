import {createHash} from "node:crypto";
import {copyFile, mkdir, readFile, writeFile} from "node:fs/promises";
import {dirname, resolve} from "node:path";
import {fileURLToPath} from "node:url";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const siteDir = resolve(scriptDir, "..");
const repoDir = resolve(siteDir, "..");
const outputDir = resolve(siteDir, "public", "firmware");
const version = process.argv[2] ?? "1.4.3";

const parts = [
  {
    name: "bootloader",
    source: resolve(repoDir, "build_cyd", "bootloader", "bootloader.bin"),
    path: "bootloader.bin",
    offset: 0x1000,
  },
  {
    name: "partition-table",
    source: resolve(
      repoDir,
      "build_cyd",
      "partition_table",
      "partition-table.bin",
    ),
    path: "partition-table.bin",
    offset: 0x8000,
  },
  {
    name: "firmware",
    source: resolve(repoDir, "build_cyd", "furi_esp32.bin"),
    path: "furi_esp32.bin",
    offset: 0x10000,
  },
];

await mkdir(outputDir, {recursive: true});

const manifestParts = [];
for (const part of parts) {
  const data = await readFile(part.source);
  const destination = resolve(outputDir, part.path);
  await copyFile(part.source, destination);
  manifestParts.push({
    name: part.name,
    path: part.path,
    offset: part.offset,
    size: data.byteLength,
    sha256: createHash("sha256").update(data).digest("hex"),
  });
}

const manifest = {
  name: "Flipper Zero ESP32 Port — CYD",
  version,
  chipFamily: "ESP32",
  flashMode: "dio",
  flashFrequency: "40m",
  flashSize: "4MB",
  parts: manifestParts,
};

await writeFile(
  resolve(outputDir, "manifest.json"),
  `${JSON.stringify(manifest, null, 2)}\n`,
);

console.log(`Firmware ${version} sincronizado em ${outputDir}`);
