"use client";

import {useEffect, useMemo, useRef, useState} from "react";
import firmwareManifest from "../public/firmware/manifest.json";
import sdcardManifest from "../public/sdcard-manifest.json";
import {CydRpcClient, type CydSerialPort} from "../lib/cyd-rpc";

type FlashState =
  | "idle"
  | "connecting"
  | "downloading"
  | "erasing"
  | "flashing"
  | "restarting"
  | "resources"
  | "verifying"
  | "done"
  | "error";

type Segment = {
  name: string;
  path: string;
  address: number;
  size: number;
  sha256: string;
};

type SdResource = {
  path: string;
  size: number;
  md5: string;
  sha256: string;
};

const DISPLAY_NAMES: Record<string, string> = {
  bootloader: "Bootloader",
  "partition-table": "Partition table",
  firmware: "Firmware",
};

const BASE_PATH = process.env.NEXT_PUBLIC_BASE_PATH?.replace(/\/$/, "") ?? "";

const SEGMENTS: Segment[] = firmwareManifest.parts.map((part) => ({
  name: DISPLAY_NAMES[part.name] ?? part.name,
  path: `${BASE_PATH}/firmware/${part.path}`,
  address: part.offset,
  size: part.size,
  sha256: part.sha256,
}));

const SD_RESOURCES: SdResource[] = sdcardManifest.files;
const FIRMWARE_PROGRESS = 65;
const RESOURCE_PROGRESS = 30;
const VERIFY_PROGRESS = 5;
const SD_FREE_SPACE_MARGIN = 64 * 1024;

const STATE_LABEL: Record<FlashState, string> = {
  idle: "Ready to connect",
  connecting: "Connecting to ESP32…",
  downloading: "Verifying the installation package…",
  erasing: "Erasing flash memory…",
  flashing: "Flashing firmware…",
  restarting: "Starting the installed firmware…",
  resources: "Installing SD card resources…",
  verifying: "Verifying SD card resources…",
  done: "Installation complete",
  error: "Installation interrupted",
};

const sleep = (milliseconds: number) =>
  new Promise((resolve) => setTimeout(resolve, milliseconds));

async function sha256Hex(bytes: Uint8Array) {
  const digest = await crypto.subtle.digest("SHA-256", Uint8Array.from(bytes).buffer);
  return Array.from(new Uint8Array(digest))
    .map((byte) => byte.toString(16).padStart(2, "0"))
    .join("");
}

async function connectRpcWithRetry(
  port: CydSerialPort,
  onRetry: (attempt: number) => void,
) {
  let lastError: unknown;

  for (let attempt = 1; attempt <= 4; attempt += 1) {
    try {
      return await CydRpcClient.connect(port);
    } catch (error) {
      lastError = error;
      if (attempt < 4) {
        onRetry(attempt + 1);
        await sleep(1200);
      }
    }
  }

  throw lastError instanceof Error
    ? lastError
    : new Error("Could not reconnect to the installed CYD firmware");
}

export default function Flasher() {
  const [state, setState] = useState<FlashState>("idle");
  const [progress, setProgress] = useState(0);
  const [detail, setDetail] = useState(
    `Firmware ${firmwareManifest.version} · CYD package`,
  );
  const [logs, setLogs] = useState<string[]>([]);
  const [webSerialSupported, setWebSerialSupported] = useState<boolean | null>(null);
  const transportRef = useRef<{
    disconnect?: () => Promise<void>;
  } | null>(null);

  const busy = !["idle", "done", "error"].includes(state);
  const totalBytes = useMemo(
    () => SEGMENTS.reduce((sum, segment) => sum + segment.size, 0),
    [],
  );
  const packageBytes = totalBytes + sdcardManifest.totalSize;

  useEffect(() => {
    setWebSerialSupported("serial" in navigator);
  }, []);

  function log(message: string) {
    setLogs((current) => [...current.slice(-39), message]);
  }

  async function install() {
    if (!webSerialSupported || busy) return;

    setProgress(0);
    setLogs([]);
    setState("connecting");
    setDetail("Select USB-SERIAL CH340 in the browser dialog");

    let firmwareFlashed = false;
    let rpc: CydRpcClient | null = null;

    try {
      const serial = (
        navigator as Navigator & {
          serial: {
            requestPort(options?: {
              filters?: {usbVendorId?: number; usbProductId?: number}[];
            }): Promise<CydSerialPort>;
          };
        }
      ).serial;

      const port: CydSerialPort = await serial.requestPort({
        filters: [{usbVendorId: 0x1a86, usbProductId: 0x7523}],
      });

      const {ESPLoader, Transport} = await import("esptool-js");
      const transport = new Transport(port as never, false);
      transportRef.current = transport;

      const terminal = {
        clean: () => setLogs([]),
        writeLine: (message: string) => log(message),
        write: (message: string) => log(message),
      };

      const loader = new ESPLoader({
        transport,
        baudrate: 460800,
        terminal,
        debugLogging: false,
      });

      const chip = await loader.main();
      log(`Detected chip: ${chip}`);
      setDetail(`${chip} connected · preparing package`);
      setState("downloading");

      const downloadedFirmware = await Promise.all(
        SEGMENTS.map(async (segment) => {
          const response = await fetch(segment.path, {cache: "no-store"});
          if (!response.ok) {
            throw new Error(`Failed to download ${segment.name} (${response.status})`);
          }

          const bytes = new Uint8Array(await response.arrayBuffer());
          if (bytes.byteLength !== segment.size) {
            throw new Error(
              `${segment.name}: invalid size (${bytes.byteLength}/${segment.size})`,
            );
          }

          const digest = await sha256Hex(bytes);
          if (digest !== segment.sha256) {
            throw new Error(`${segment.name}: invalid SHA-256 checksum`);
          }
          log(`${segment.name} verified (${bytes.byteLength} bytes)`);

          return {
            data: bytes,
            address: segment.address,
          };
        }),
      );

      const downloadedResources = await Promise.all(
        SD_RESOURCES.map(async (resource) => {
          const resourceUrl = `${BASE_PATH}/sdcard/${resource.path}`;
          const response = await fetch(resourceUrl, {cache: "no-store"});
          if (!response.ok) {
            throw new Error(
              `Failed to download SD resource ${resource.path} (${response.status})`,
            );
          }

          const bytes = new Uint8Array(await response.arrayBuffer());
          if (bytes.byteLength !== resource.size) {
            throw new Error(
              `${resource.path}: invalid size (${bytes.byteLength}/${resource.size})`,
            );
          }

          const digest = await sha256Hex(bytes);
          if (digest !== resource.sha256) {
            throw new Error(`${resource.path}: invalid SHA-256 checksum`);
          }

          return {...resource, data: bytes};
        }),
      );
      log(
        `SD starter pack verified (${SD_RESOURCES.length} files, ${sdcardManifest.totalSize} bytes)`,
      );

      setState("erasing");
      setDetail("Erasing all 4 MB for a clean installation");
      await loader.eraseFlash();

      setState("flashing");
      setDetail("Do not unplug the USB cable");

      const completedByFile = new Array(SEGMENTS.length).fill(0);
      await loader.writeFlash({
        fileArray: downloadedFirmware,
        flashSize: "4MB",
        flashMode: "dio",
        flashFreq: "40m",
        eraseAll: false,
        compress: true,
        reportProgress: (fileIndex: number, written: number) => {
          completedByFile[fileIndex] = written;
          const completed = completedByFile.reduce(
            (sum: number, value: number) => sum + value,
            0,
          );
          setProgress(
            Math.min(
              FIRMWARE_PROGRESS,
              Math.round((completed / totalBytes) * FIRMWARE_PROGRESS),
            ),
          );
        },
      });
      firmwareFlashed = true;
      setProgress(FIRMWARE_PROGRESS);
      log("Firmware flash completed and verified.");

      setState("restarting");
      setDetail("Rebooting the CYD and opening its storage RPC");
      await loader.after("hard_reset");
      await transport.disconnect();
      transportRef.current = null;
      await sleep(2800);

      rpc = await connectRpcWithRetry(port, (attempt) => {
        log(`CYD RPC not ready; reconnect attempt ${attempt}/4`);
      });
      const storage = await rpc.storageInfo();
      if (storage.free < sdcardManifest.totalSize + SD_FREE_SPACE_MARGIN) {
        throw new Error(
          `Not enough free space on the SD card (${storage.free} bytes available)`,
        );
      }
      log(
        `SD card ready (${storage.free} bytes free of ${storage.total} bytes)`,
      );

      setState("resources");
      setDetail(
        `Creating folders and copying ${SD_RESOURCES.length} required files`,
      );
      for (const directory of sdcardManifest.directories) {
        await rpc.mkdir(`${sdcardManifest.targetRoot}/${directory}`);
      }

      let completedResourceBytes = 0;
      for (const resource of downloadedResources) {
        const devicePath = `${sdcardManifest.targetRoot}/${resource.path}`;
        await rpc.writeFile(devicePath, resource.data, (written) => {
          const ratio =
            (completedResourceBytes + written) / sdcardManifest.totalSize;
          setProgress(
            FIRMWARE_PROGRESS +
              Math.min(
                RESOURCE_PROGRESS,
                Math.round(ratio * RESOURCE_PROGRESS),
              ),
          );
        });
        completedResourceBytes += resource.size;
        log(`Copied ${devicePath}`);
      }

      setProgress(FIRMWARE_PROGRESS + RESOURCE_PROGRESS);
      setState("verifying");
      setDetail("Checking every installed file against its MD5 checksum");
      for (let index = 0; index < downloadedResources.length; index += 1) {
        const resource = downloadedResources[index];
        const devicePath = `${sdcardManifest.targetRoot}/${resource.path}`;
        const deviceMd5 = (await rpc.md5(devicePath)).toLowerCase();
        if (deviceMd5 !== resource.md5) {
          throw new Error(
            `${resource.path}: SD verification failed (${deviceMd5 || "no checksum"})`,
          );
        }
        setProgress(
          FIRMWARE_PROGRESS +
            RESOURCE_PROGRESS +
            Math.round(((index + 1) / downloadedResources.length) * VERIFY_PROGRESS),
        );
      }

      await rpc.close();
      rpc = null;

      setProgress(100);
      setState("done");
      setDetail(
        `Firmware ${firmwareManifest.version} and ${SD_RESOURCES.length} SD resources installed.`,
      );
      log("Firmware and SD starter pack installation completed successfully.");
    } catch (error) {
      const message =
        error instanceof Error ? error.message : "Unknown error while flashing";
      setState("error");
      setDetail(
        firmwareFlashed
          ? `Firmware installed, but SD setup failed: ${message}`
          : message,
      );
      log(`ERROR: ${message}`);
      try {
        await rpc?.close();
      } catch {
        // The serial stream may already have closed after an RPC failure.
      }
      try {
        await transportRef.current?.disconnect?.();
      } catch {
        // The browser may already have closed the serial stream.
      }
      transportRef.current = null;
    }
  }

  return (
    <section className="flasher-section" aria-labelledby="flasher-title">
      <div className="flasher-heading">
        <div>
          <p className="section-number">01 / INSTALL</p>
          <h2 id="flasher-title">Web Flasher</h2>
        </div>
        <span className={`status-pill status-${state}`}>
          <i aria-hidden="true" />
          {state === "done" ? "Complete" : busy ? "In progress" : "Online"}
        </span>
      </div>

      <div className="flasher-card">
        <div className="flash-main">
          <div className="flash-status">
            <span className="flash-icon" aria-hidden="true">
              {state === "done" ? "✓" : state === "error" ? "!" : "↯"}
            </span>
            <div>
              <h3>{STATE_LABEL[state]}</h3>
              <p>{detail}</p>
            </div>
          </div>

          <div
            className="progress-track"
            role="progressbar"
            aria-valuemin={0}
            aria-valuemax={100}
            aria-valuenow={progress}
          >
            <span style={{width: `${progress}%`}} />
          </div>
          <div className="progress-meta">
            <span>{progress}%</span>
            <span>{(packageBytes / 1024 / 1024).toFixed(2)} MB</span>
          </div>

          {webSerialSupported === null ? (
            <div className="browser-warning">Checking browser support…</div>
          ) : !webSerialSupported ? (
            <div className="browser-warning">
              Web Serial is unavailable. Open this page in Chrome or Edge on a
              desktop computer.
            </div>
          ) : (
            <button className="install-button" onClick={install} disabled={busy}>
              <span>
                {busy
                  ? "Installing…"
                  : state === "done"
                    ? "Install again"
                    : "Connect and install"}
              </span>
              <b aria-hidden="true">→</b>
            </button>
          )}

          <p className="privacy-note">
            Firmware and SD resources are transferred locally. No serial-port
            data is sent to the server.
          </p>
        </div>

        <aside className="flash-package">
          <p>PACKAGE CONTENTS</p>
          {SEGMENTS.map((segment) => (
            <div className="package-row" key={segment.name}>
              <span>
                <i />
                {segment.name}
              </span>
              <code>0x{segment.address.toString(16).toUpperCase()}</code>
            </div>
          ))}
          <div className="package-row">
            <span>
              <i />
              SD starter pack
            </span>
            <code>{SD_RESOURCES.length} FILES</code>
          </div>
          <div className="package-footer">
            <span>Mode</span>
            <strong>FLASH + FAT32 SD</strong>
          </div>
        </aside>
      </div>

      {logs.length > 0 && (
        <details className="flash-log">
          <summary>Technical log ({logs.length})</summary>
          <pre>{logs.join("\n")}</pre>
        </details>
      )}
    </section>
  );
}
