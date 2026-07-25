"use client";

import {useEffect, useMemo, useRef, useState} from "react";
import firmwareManifest from "../public/firmware/manifest.json";

type FlashState =
  | "idle"
  | "connecting"
  | "downloading"
  | "erasing"
  | "flashing"
  | "restarting"
  | "done"
  | "error";

type Segment = {
  name: string;
  path: string;
  address: number;
  size: number;
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

const STATE_LABEL: Record<FlashState, string> = {
  idle: "Ready to connect",
  connecting: "Connecting to ESP32…",
  downloading: "Downloading and verifying firmware…",
  erasing: "Erasing flash memory…",
  flashing: "Flashing firmware…",
  restarting: "Restarting your CYD…",
  done: "Firmware installed",
  error: "Installation interrupted",
};

async function sha256Hex(bytes: Uint8Array) {
  const digest = await crypto.subtle.digest("SHA-256", Uint8Array.from(bytes).buffer);
  return Array.from(new Uint8Array(digest))
    .map((byte) => byte.toString(16).padStart(2, "0"))
    .join("");
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

    try {
      const serial = (
        navigator as Navigator & {
          serial: {
            requestPort(options?: {
              filters?: {usbVendorId?: number; usbProductId?: number}[];
            }): Promise<unknown>;
          };
        }
      ).serial;

      const port = await serial.requestPort({
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

      const downloaded = await Promise.all(
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

      setState("erasing");
      setDetail("Erasing all 4 MB for a clean installation");
      await loader.eraseFlash();

      setState("flashing");
      setDetail("Do not unplug the USB cable");

      const completedByFile = new Array(SEGMENTS.length).fill(0);
      await loader.writeFlash({
        fileArray: downloaded,
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
          setProgress(Math.min(100, Math.round((completed / totalBytes) * 100)));
        },
      });

      setProgress(100);
      setState("restarting");
      setDetail("Starting the installed firmware");
      await loader.after("hard_reset");
      await transport.disconnect();
      transportRef.current = null;

      setState("done");
      setDetail(
        `Firmware ${firmwareManifest.version} installed. Prepare the SD card below.`,
      );
      log("Flash completed, verified, and the CYD was restarted.");
    } catch (error) {
      const message =
        error instanceof Error ? error.message : "Unknown error while flashing";
      setState("error");
      setDetail(message);
      log(`ERROR: ${message}`);
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
          <p className="section-number">01 / FLASH</p>
          <h2 id="flasher-title">Install the firmware</h2>
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
            <span>{(totalBytes / 1024 / 1024).toFixed(2)} MB</span>
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
                  ? "Flashing…"
                  : state === "done"
                    ? "Flash again"
                    : "Connect and flash"}
              </span>
              <b aria-hidden="true">→</b>
            </button>
          )}

          <p className="privacy-note">
            Flashing happens locally. No serial-port data is sent to the server.
          </p>
        </div>

        <aside className="flash-package">
          <p>FIRMWARE CONTENTS</p>
          {SEGMENTS.map((segment) => (
            <div className="package-row" key={segment.name}>
              <span>
                <i />
                {segment.name}
              </span>
              <code>0x{segment.address.toString(16).toUpperCase()}</code>
            </div>
          ))}
          <div className="package-footer">
            <span>Mode</span>
            <strong>DIO / 40 MHz</strong>
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
