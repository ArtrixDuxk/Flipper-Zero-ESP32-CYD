"use client";

import {useEffect, useMemo, useRef, useState} from "react";
import firmwareManifest from "../public/firmware/manifest.json";

type FlashState =
  | "idle"
  | "connecting"
  | "downloading"
  | "erasing"
  | "flashing"
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

const SEGMENTS: Segment[] = firmwareManifest.parts.map((part) => ({
  name: DISPLAY_NAMES[part.name] ?? part.name,
  path: `/firmware/${part.path}`,
  address: part.offset,
  size: part.size,
  sha256: part.sha256,
}));

const STATE_LABEL: Record<FlashState, string> = {
  idle: "Pronto para conectar",
  connecting: "Conectando ao ESP32…",
  downloading: "Baixando e verificando firmware…",
  erasing: "Apagando a memória flash…",
  flashing: "Gravando firmware…",
  done: "Instalação concluída",
  error: "A instalação foi interrompida",
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
    `Firmware ${firmwareManifest.version} · pacote CYD`,
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
    setDetail("Selecione USB-SERIAL CH340 na janela do navegador");

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
      log(`Chip detectado: ${chip}`);
      setDetail(`${chip} conectado · preparando pacote`);
      setState("downloading");

      const downloaded = await Promise.all(
        SEGMENTS.map(async (segment) => {
          const response = await fetch(segment.path, {cache: "no-store"});
          if (!response.ok) {
            throw new Error(`Falha ao baixar ${segment.name} (${response.status})`);
          }

          const bytes = new Uint8Array(await response.arrayBuffer());
          if (bytes.byteLength !== segment.size) {
            throw new Error(
              `${segment.name}: tamanho inválido (${bytes.byteLength}/${segment.size})`,
            );
          }

          const digest = await sha256Hex(bytes);
          if (digest !== segment.sha256) {
            throw new Error(`${segment.name}: checksum SHA-256 inválido`);
          }
          log(`${segment.name} verificado (${bytes.byteLength} bytes)`);

          return {
            data: bytes,
            address: segment.address,
          };
        }),
      );

      setState("erasing");
      setDetail("Apagando os 4 MB para uma instalação limpa");
      await loader.eraseFlash();

      setState("flashing");
      setDetail("Não remova o cabo USB");

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
      setState("done");
      setDetail(
        `Firmware ${firmwareManifest.version} instalado. A CYD irá reiniciar.`,
      );
      log("Gravação concluída e verificada.");

      await transport.disconnect();
      transportRef.current = null;
    } catch (error) {
      const message =
        error instanceof Error ? error.message : "Erro desconhecido durante a gravação";
      setState("error");
      setDetail(message);
      log(`ERRO: ${message}`);
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
          <p className="section-number">01 / INSTALAÇÃO</p>
          <h2 id="flasher-title">Web Flasher</h2>
        </div>
        <span className={`status-pill status-${state}`}>
          <i aria-hidden="true" />
          {state === "done" ? "Finalizado" : busy ? "Em andamento" : "Online"}
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
            <div className="browser-warning">Verificando suporte do navegador…</div>
          ) : !webSerialSupported ? (
            <div className="browser-warning">
              Web Serial não está disponível. Abra esta página no Chrome ou Edge
              em um computador.
            </div>
          ) : (
            <button className="install-button" onClick={install} disabled={busy}>
              <span>{busy ? "Instalando…" : state === "done" ? "Instalar novamente" : "Conectar e instalar"}</span>
              <b aria-hidden="true">→</b>
            </button>
          )}

          <p className="privacy-note">
            A gravação acontece localmente. Nenhum dado da sua porta serial é enviado
            ao servidor.
          </p>
        </div>

        <aside className="flash-package">
          <p>CONTEÚDO DO PACOTE</p>
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
            <span>Modo</span>
            <strong>DIO / 40 MHz</strong>
          </div>
        </aside>
      </div>

      {logs.length > 0 && (
        <details className="flash-log">
          <summary>Log técnico ({logs.length})</summary>
          <pre>{logs.join("\n")}</pre>
        </details>
      )}
    </section>
  );
}
