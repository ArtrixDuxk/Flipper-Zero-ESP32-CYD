import {createHash} from "node:crypto";
import {
  cp,
  mkdir,
  readFile,
  readdir,
  rm,
  stat,
  writeFile,
} from "node:fs/promises";
import path from "node:path";
import {fileURLToPath} from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const webRoot = path.resolve(scriptDir, "..");
const projectRoot = path.resolve(webRoot, "..");
const outputRoot = path.join(webRoot, "public", "sdcard");
const manifestPath = path.join(webRoot, "public", "sdcard-manifest.json");

const resourceRoots = [
  "applications/main/bad_usb/resources",
  "applications/main/infrared/resources",
  "applications/main/lfrfid/resources",
  "applications/main/nfc/resources",
  "applications/main/subghz/resources",
  "applications/main/u2f/resources",
];

const emptyDirectories = [
  "apps",
  "apps/Scripts",
  "apps_data",
  "badusb",
  "dolphin",
  "infrared",
  "lfrfid",
  "nfc",
  "subghz",
  "u2f",
  "wifi",
  "wifi/evil_portal",
  "wifi/evil_portal/login_template",
  "wifi/evil_portal/router_template",
];

async function walkFiles(directory, prefix = "") {
  const entries = await readdir(directory, {withFileTypes: true});
  const files = [];

  for (const entry of entries.sort((a, b) => a.name.localeCompare(b.name))) {
    const relative = path.posix.join(prefix, entry.name);
    const absolute = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      files.push(...(await walkFiles(absolute, relative)));
    } else if (entry.isFile()) {
      files.push({absolute, relative});
    }
  }

  return files;
}

await rm(outputRoot, {recursive: true, force: true});
await mkdir(outputRoot, {recursive: true});

for (const resourceRoot of resourceRoots) {
  const source = path.join(projectRoot, resourceRoot);
  await cp(source, outputRoot, {recursive: true});
}

for (const directory of emptyDirectories) {
  await mkdir(path.join(outputRoot, directory), {recursive: true});
}

const resourceFiles = await walkFiles(outputRoot);
const directories = new Set(emptyDirectories);
for (const {relative} of resourceFiles) {
  let parent = path.posix.dirname(relative);
  while (parent !== ".") {
    directories.add(parent);
    parent = path.posix.dirname(parent);
  }
}

const sortedDirectories = [...directories].sort((a, b) => {
  const depth = a.split("/").length - b.split("/").length;
  return depth || a.localeCompare(b);
});

const generatedAt = Math.floor(Date.now() / 1000);
const deviceManifestLines = [
  "V:0",
  `T:${generatedAt}`,
  ...sortedDirectories.map((directory) => `D:${directory}`),
];

const files = [];
for (const {absolute, relative} of resourceFiles) {
  const data = await readFile(absolute);
  const info = await stat(absolute);
  const md5 = createHash("md5").update(data).digest("hex");
  const sha256 = createHash("sha256").update(data).digest("hex");
  deviceManifestLines.push(`F:${md5}:${info.size}:${relative}`);
  files.push({path: relative, size: info.size, md5, sha256});
}

const deviceManifest = `${deviceManifestLines.join("\n")}\n`;
await writeFile(path.join(outputRoot, "Manifest"), deviceManifest);

const manifestData = await readFile(path.join(outputRoot, "Manifest"));
files.push({
  path: "Manifest",
  size: manifestData.byteLength,
  md5: createHash("md5").update(manifestData).digest("hex"),
  sha256: createHash("sha256").update(manifestData).digest("hex"),
});

files.sort((a, b) => {
  if (a.path === "Manifest") return 1;
  if (b.path === "Manifest") return -1;
  return a.path.localeCompare(b.path);
});

const manifest = {
  name: "Flipper Zero ESP32 Port — CYD SD resources",
  version: "1.0.0",
  generatedAt,
  targetRoot: "/ext",
  totalSize: files.reduce((sum, file) => sum + file.size, 0),
  directories: sortedDirectories,
  files,
};

await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
console.log(
  `Synced ${files.length} files (${manifest.totalSize} bytes) to ${outputRoot}`,
);
