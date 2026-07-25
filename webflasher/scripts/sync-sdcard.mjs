import {createHash} from "node:crypto";
import {
  cp,
  mkdir,
  mkdtemp,
  readFile,
  readdir,
  rm,
  stat,
  writeFile,
} from "node:fs/promises";
import {tmpdir} from "node:os";
import path from "node:path";
import {fileURLToPath} from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const webRoot = path.resolve(scriptDir, "..");
const projectRoot = path.resolve(webRoot, "..");
const manifestPath = path.join(webRoot, "public", "sdcard-manifest.json");
const downloadDirectory = path.join(webRoot, "public", "downloads");
const archivePath = path.join(downloadDirectory, "flipper-cyd-sd-card.zip");

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

const crcTable = new Uint32Array(256);
for (let index = 0; index < 256; index += 1) {
  let value = index;
  for (let bit = 0; bit < 8; bit += 1) {
    value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
  }
  crcTable[index] = value >>> 0;
}

function crc32(data) {
  let value = 0xffffffff;
  for (const byte of data) {
    value = crcTable[(value ^ byte) & 0xff] ^ (value >>> 8);
  }
  return (value ^ 0xffffffff) >>> 0;
}

function dosTimestamp(unixTimestamp) {
  const date = new Date(unixTimestamp * 1000);
  const year = Math.max(1980, date.getUTCFullYear());
  return {
    date:
      ((year - 1980) << 9) |
      ((date.getUTCMonth() + 1) << 5) |
      date.getUTCDate(),
    time:
      (date.getUTCHours() << 11) |
      (date.getUTCMinutes() << 5) |
      Math.floor(date.getUTCSeconds() / 2),
  };
}

function createZip(entries, unixTimestamp) {
  const localParts = [];
  const centralParts = [];
  const {date, time} = dosTimestamp(unixTimestamp);
  let localOffset = 0;

  for (const entry of entries) {
    const name = Buffer.from(entry.name, "utf8");
    const data = entry.data;
    const checksum = crc32(data);
    const localHeader = Buffer.alloc(30);

    localHeader.writeUInt32LE(0x04034b50, 0);
    localHeader.writeUInt16LE(20, 4);
    localHeader.writeUInt16LE(0x0800, 6);
    localHeader.writeUInt16LE(0, 8);
    localHeader.writeUInt16LE(time, 10);
    localHeader.writeUInt16LE(date, 12);
    localHeader.writeUInt32LE(checksum, 14);
    localHeader.writeUInt32LE(data.length, 18);
    localHeader.writeUInt32LE(data.length, 22);
    localHeader.writeUInt16LE(name.length, 26);
    localHeader.writeUInt16LE(0, 28);

    localParts.push(localHeader, name, data);

    const centralHeader = Buffer.alloc(46);
    centralHeader.writeUInt32LE(0x02014b50, 0);
    centralHeader.writeUInt16LE(0x0314, 4);
    centralHeader.writeUInt16LE(20, 6);
    centralHeader.writeUInt16LE(0x0800, 8);
    centralHeader.writeUInt16LE(0, 10);
    centralHeader.writeUInt16LE(time, 12);
    centralHeader.writeUInt16LE(date, 14);
    centralHeader.writeUInt32LE(checksum, 16);
    centralHeader.writeUInt32LE(data.length, 20);
    centralHeader.writeUInt32LE(data.length, 24);
    centralHeader.writeUInt16LE(name.length, 28);
    centralHeader.writeUInt16LE(0, 30);
    centralHeader.writeUInt16LE(0, 32);
    centralHeader.writeUInt16LE(0, 34);
    centralHeader.writeUInt16LE(0, 36);
    centralHeader.writeUInt32LE(
      entry.directory ? ((0o40755 << 16) | 0x10) >>> 0 : (0o100644 << 16) >>> 0,
      38,
    );
    centralHeader.writeUInt32LE(localOffset, 42);
    centralParts.push(centralHeader, name);

    localOffset += localHeader.length + name.length + data.length;
  }

  const centralDirectory = Buffer.concat(centralParts);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(0, 4);
  end.writeUInt16LE(0, 6);
  end.writeUInt16LE(entries.length, 8);
  end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(centralDirectory.length, 12);
  end.writeUInt32LE(localOffset, 16);
  end.writeUInt16LE(0, 20);

  return Buffer.concat([...localParts, centralDirectory, end]);
}

async function selectGeneratedAt(files, directories) {
  const sourceDateEpoch = Number.parseInt(process.env.SOURCE_DATE_EPOCH ?? "", 10);
  if (Number.isSafeInteger(sourceDateEpoch) && sourceDateEpoch > 0) {
    return sourceDateEpoch;
  }

  try {
    const previous = JSON.parse(await readFile(manifestPath, "utf8"));
    const previousFiles = previous.files.filter((file) => file.path !== "Manifest");
    if (
      JSON.stringify(previousFiles) === JSON.stringify(files) &&
      JSON.stringify(previous.directories) === JSON.stringify(directories) &&
      Number.isSafeInteger(previous.generatedAt)
    ) {
      return previous.generatedAt;
    }
  } catch {
    // A missing or invalid previous manifest means this is a fresh package.
  }

  return Math.floor(Date.now() / 1000);
}

const stagingRoot = await mkdtemp(path.join(tmpdir(), "flipper-cyd-sd-"));

try {
  for (const resourceRoot of resourceRoots) {
    const source = path.join(projectRoot, resourceRoot);
    await cp(source, stagingRoot, {recursive: true});
  }

  for (const directory of emptyDirectories) {
    await mkdir(path.join(stagingRoot, directory), {recursive: true});
  }

  const resourceFiles = await walkFiles(stagingRoot);
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

  const files = [];
  const archiveFiles = [];
  for (const {absolute, relative} of resourceFiles) {
    const data = await readFile(absolute);
    const info = await stat(absolute);
    const md5 = createHash("md5").update(data).digest("hex");
    const sha256 = createHash("sha256").update(data).digest("hex");
    files.push({path: relative, size: info.size, md5, sha256});
    archiveFiles.push({name: relative, data, directory: false});
  }

  files.sort((a, b) => a.path.localeCompare(b.path));
  archiveFiles.sort((a, b) => a.name.localeCompare(b.name));

  const generatedAt = await selectGeneratedAt(files, sortedDirectories);
  const deviceManifestLines = [
    "V:0",
    `T:${generatedAt}`,
    ...sortedDirectories.map((directory) => `D:${directory}`),
    ...files.map((file) => `F:${file.md5}:${file.size}:${file.path}`),
  ];

  const deviceManifest = Buffer.from(`${deviceManifestLines.join("\n")}\n`);
  files.push({
    path: "Manifest",
    size: deviceManifest.byteLength,
    md5: createHash("md5").update(deviceManifest).digest("hex"),
    sha256: createHash("sha256").update(deviceManifest).digest("hex"),
  });
  archiveFiles.push({name: "Manifest", data: deviceManifest, directory: false});

  const manifest = {
    name: "Flipper Zero ESP32 Port — CYD SD resources",
    version: "1.0.0",
    generatedAt,
    targetRoot: "/ext",
    totalSize: files.reduce((sum, file) => sum + file.size, 0),
    directories: sortedDirectories,
    files,
  };

  const archiveEntries = [
    ...sortedDirectories.map((directory) => ({
      name: `${directory}/`,
      data: Buffer.alloc(0),
      directory: true,
    })),
    ...archiveFiles,
  ];

  await mkdir(downloadDirectory, {recursive: true});
  await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  await writeFile(archivePath, createZip(archiveEntries, generatedAt));

  console.log(
    `Created ${path.relative(webRoot, archivePath)} with ${files.length} files (${manifest.totalSize} bytes)`,
  );
} finally {
  await rm(stagingRoot, {recursive: true, force: true});
}
