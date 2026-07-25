const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

const RPC_START_ECHO = textEncoder.encode("start_rpc_session\r\n");
const WRITE_CHUNK_SIZE = 512;
const WRITE_CHUNKS_PER_PING = 8;
const SERIAL_BOOT_SETTLE_MS = 2500;

const MAIN_TAG = {
  empty: 4,
  pingRequest: 5,
  pingResponse: 6,
  storageWriteRequest: 11,
  storageMkdirRequest: 13,
  storageMd5Request: 14,
  storageMd5Response: 15,
  stopSession: 19,
  storageInfoRequest: 28,
  storageInfoResponse: 29,
} as const;

const STATUS_TEXT: Record<number, string> = {
  1: "Unknown RPC error",
  2: "RPC decode failure",
  3: "RPC command is not implemented",
  4: "Device is busy",
  5: "SD card is not ready",
  6: "File or directory already exists",
  7: "File or directory does not exist",
  8: "Invalid storage parameter",
  9: "Storage access denied",
  10: "Invalid storage path",
  11: "Internal storage error",
  12: "Storage command is not implemented",
  13: "File or directory is already open",
  14: "Continuous RPC command was interrupted",
  15: "Invalid RPC parameters",
  18: "Directory is not empty",
};

export type CydSerialPort = {
  readable: ReadableStream<Uint8Array> | null;
  writable: WritableStream<Uint8Array> | null;
  open(options: {baudRate: number; bufferSize?: number}): Promise<void>;
  close(): Promise<void>;
  setSignals?(signals: {
    dataTerminalReady?: boolean;
    requestToSend?: boolean;
  }): Promise<void>;
};

type ParsedField = {
  tag: number;
  wireType: number;
  value?: number;
  bytes?: Uint8Array;
};

type RpcResponse = {
  commandId: number;
  status: number;
  hasNext: boolean;
  contentTag?: number;
  content?: Uint8Array;
};

function concatBytes(...parts: Uint8Array[]) {
  const length = parts.reduce((sum, part) => sum + part.byteLength, 0);
  const result = new Uint8Array(length);
  let offset = 0;
  for (const part of parts) {
    result.set(part, offset);
    offset += part.byteLength;
  }
  return result;
}

function encodeVarint(value: number) {
  const bytes: number[] = [];
  let remaining = Math.floor(value);
  do {
    let byte = remaining % 128;
    remaining = Math.floor(remaining / 128);
    if (remaining) byte |= 0x80;
    bytes.push(byte);
  } while (remaining);
  return Uint8Array.from(bytes);
}

function encodeVarintField(tag: number, value: number) {
  return concatBytes(encodeVarint(tag << 3), encodeVarint(value));
}

function encodeBytesField(tag: number, value: Uint8Array) {
  return concatBytes(
    encodeVarint((tag << 3) | 2),
    encodeVarint(value.byteLength),
    value,
  );
}

function encodeStringField(tag: number, value: string) {
  return encodeBytesField(tag, textEncoder.encode(value));
}

function encodeDelimitedMain(
  commandId: number,
  contentTag: number,
  content: Uint8Array,
  hasNext = false,
) {
  const body = concatBytes(
    encodeVarintField(1, commandId),
    hasNext ? encodeVarintField(3, 1) : new Uint8Array(),
    encodeBytesField(contentTag, content),
  );
  return concatBytes(encodeVarint(body.byteLength), body);
}

function readVarint(
  bytes: Uint8Array,
  start: number,
): {value: number; next: number} | null {
  let value = 0;
  let multiplier = 1;

  for (let offset = start; offset < bytes.byteLength; offset += 1) {
    const byte = bytes[offset];
    value += (byte & 0x7f) * multiplier;
    if ((byte & 0x80) === 0) return {value, next: offset + 1};
    multiplier *= 128;
    if (multiplier > Number.MAX_SAFE_INTEGER) {
      throw new Error("RPC varint exceeds JavaScript's safe integer range");
    }
  }

  return null;
}

function parseFields(bytes: Uint8Array) {
  const fields: ParsedField[] = [];
  let offset = 0;

  while (offset < bytes.byteLength) {
    const key = readVarint(bytes, offset);
    if (!key) throw new Error("Incomplete RPC field key");
    offset = key.next;
    const tag = Math.floor(key.value / 8);
    const wireType = key.value & 7;

    if (wireType === 0) {
      const value = readVarint(bytes, offset);
      if (!value) throw new Error("Incomplete RPC varint");
      fields.push({tag, wireType, value: value.value});
      offset = value.next;
    } else if (wireType === 2) {
      const length = readVarint(bytes, offset);
      if (!length) throw new Error("Incomplete RPC field length");
      const end = length.next + length.value;
      if (end > bytes.byteLength) throw new Error("Incomplete RPC byte field");
      fields.push({
        tag,
        wireType,
        bytes: bytes.slice(length.next, end),
      });
      offset = end;
    } else if (wireType === 1) {
      offset += 8;
    } else if (wireType === 5) {
      offset += 4;
    } else {
      throw new Error(`Unsupported RPC protobuf wire type ${wireType}`);
    }

    if (offset > bytes.byteLength) throw new Error("Incomplete RPC fixed-width field");
  }

  return fields;
}

function parseMainResponse(body: Uint8Array): RpcResponse {
  const fields = parseFields(body);
  const commandId = fields.find((field) => field.tag === 1)?.value ?? 0;
  const status = fields.find((field) => field.tag === 2)?.value ?? 0;
  const hasNext = Boolean(fields.find((field) => field.tag === 3)?.value);
  const content = fields.find((field) => field.tag >= 4 && field.wireType === 2);

  return {
    commandId,
    status,
    hasNext,
    contentTag: content?.tag,
    content: content?.bytes,
  };
}

function findSequence(haystack: Uint8Array, needle: Uint8Array) {
  outer: for (let start = 0; start <= haystack.byteLength - needle.byteLength; start += 1) {
    for (let index = 0; index < needle.byteLength; index += 1) {
      if (haystack[start + index] !== needle[index]) continue outer;
    }
    return start;
  }
  return -1;
}

function statusError(status: number) {
  return new Error(STATUS_TEXT[status] ?? `RPC failed with status ${status}`);
}

const sleep = (milliseconds: number) =>
  new Promise((resolve) => setTimeout(resolve, milliseconds));

export class CydRpcClient {
  private readonly port: CydSerialPort;
  private readonly reader: ReadableStreamDefaultReader<Uint8Array>;
  private readonly writer: WritableStreamDefaultWriter<Uint8Array>;
  private receiveBuffer = new Uint8Array();
  private commandId = 0;
  private sessionStarted = false;

  private constructor(
    port: CydSerialPort,
    reader: ReadableStreamDefaultReader<Uint8Array>,
    writer: WritableStreamDefaultWriter<Uint8Array>,
  ) {
    this.port = port;
    this.reader = reader;
    this.writer = writer;
  }

  static async connect(port: CydSerialPort) {
    await port.open({baudRate: 115200, bufferSize: 65536});

    if (!port.readable || !port.writable) {
      await port.close();
      throw new Error("Serial streams are unavailable after reopening the CYD");
    }

    const client = new CydRpcClient(
      port,
      port.readable.getReader(),
      port.writable.getWriter(),
    );
    try {
      /*
       * Opening a CYD's CH340 can pulse EN through its auto-reset circuit.
       * Starting the text handshake immediately loses the command in the ROM
       * boot log, then each reconnect attempt pulses reset again. Leave modem
       * control lines untouched and wait for the firmware/SD mount to settle.
       */
      await sleep(SERIAL_BOOT_SETTLE_MS);
      await client.startSession();
      client.sessionStarted = true;
      return client;
    } catch (error) {
      await client.close(false);
      throw error;
    }
  }

  private nextCommandId() {
    this.commandId = (this.commandId + 1) >>> 0;
    if (this.commandId === 0) this.commandId = 1;
    return this.commandId;
  }

  private async readChunk(timeoutMs: number) {
    let timeout: ReturnType<typeof setTimeout> | undefined;
    try {
      const result = await Promise.race([
        this.reader.read(),
        new Promise<never>((_, reject) => {
          timeout = setTimeout(
            () => reject(new Error("Timed out waiting for the CYD RPC response")),
            timeoutMs,
          );
        }),
      ]);
      if (result.done) throw new Error("The CYD closed the serial connection");
      return result.value;
    } finally {
      if (timeout) clearTimeout(timeout);
    }
  }

  private async startSession() {
    await this.writer.write(textEncoder.encode("start_rpc_session\r"));
    let received = new Uint8Array();
    const deadline = Date.now() + 8000;

    while (Date.now() < deadline) {
      const chunk = await this.readChunk(Math.max(1, deadline - Date.now()));
      received = concatBytes(received, chunk);
      const echoOffset = findSequence(received, RPC_START_ECHO);
      if (echoOffset >= 0) {
        this.receiveBuffer = received.slice(echoOffset + RPC_START_ECHO.byteLength);
        return;
      }
      if (received.byteLength > 16384) received = received.slice(-4096);
    }

    throw new Error("The flashed firmware did not start its RPC session");
  }

  private async readFrame(timeoutMs = 10000) {
    const deadline = Date.now() + timeoutMs;

    while (Date.now() < deadline) {
      const length = readVarint(this.receiveBuffer, 0);
      if (length && this.receiveBuffer.byteLength >= length.next + length.value) {
        const end = length.next + length.value;
        const body = this.receiveBuffer.slice(length.next, end);
        this.receiveBuffer = this.receiveBuffer.slice(end);
        return parseMainResponse(body);
      }

      const chunk = await this.readChunk(Math.max(1, deadline - Date.now()));
      this.receiveBuffer = concatBytes(this.receiveBuffer, chunk);
    }

    throw new Error("Timed out waiting for a complete CYD RPC frame");
  }

  private async waitForResponse(
    commandId: number,
    expectedTag?: number,
    timeoutMs = 10000,
  ) {
    const deadline = Date.now() + timeoutMs;

    while (Date.now() < deadline) {
      const response = await this.readFrame(Math.max(1, deadline - Date.now()));
      if (response.commandId !== commandId) continue;
      if (expectedTag !== undefined && response.contentTag !== expectedTag) continue;
      return response;
    }

    throw new Error(`Timed out waiting for RPC command ${commandId}`);
  }

  async storageInfo() {
    const commandId = this.nextCommandId();
    const request = encodeStringField(1, "/ext");
    await this.writer.write(
      encodeDelimitedMain(commandId, MAIN_TAG.storageInfoRequest, request),
    );
    const response = await this.waitForResponse(
      commandId,
      MAIN_TAG.storageInfoResponse,
    );
    if (response.status !== 0) throw statusError(response.status);

    const fields = parseFields(response.content ?? new Uint8Array());
    return {
      total: fields.find((field) => field.tag === 1)?.value ?? 0,
      free: fields.find((field) => field.tag === 2)?.value ?? 0,
    };
  }

  async mkdir(path: string) {
    const commandId = this.nextCommandId();
    await this.writer.write(
      encodeDelimitedMain(
        commandId,
        MAIN_TAG.storageMkdirRequest,
        encodeStringField(1, path),
      ),
    );
    const response = await this.waitForResponse(commandId, MAIN_TAG.empty);
    if (response.status !== 0 && response.status !== 6) {
      throw statusError(response.status);
    }
  }

  async writeFile(
    path: string,
    data: Uint8Array,
    onProgress?: (written: number) => void,
  ) {
    const commandId = this.nextCommandId();
    let offset = 0;
    let chunksSincePing = 0;

    do {
      const end = Math.min(data.byteLength, offset + WRITE_CHUNK_SIZE);
      const chunk = data.slice(offset, end);
      const hasNext = end < data.byteLength;
      const file = chunk.byteLength
        ? encodeBytesField(4, chunk)
        : new Uint8Array();
      const request = concatBytes(
        encodeStringField(1, path),
        file.byteLength ? encodeBytesField(2, file) : new Uint8Array(),
      );

      await this.writer.write(
        encodeDelimitedMain(
          commandId,
          MAIN_TAG.storageWriteRequest,
          request,
          hasNext,
        ),
      );
      offset = end;
      chunksSincePing += 1;
      onProgress?.(offset);

      if (hasNext && chunksSincePing >= WRITE_CHUNKS_PER_PING) {
        await this.writer.write(
          encodeDelimitedMain(commandId, MAIN_TAG.pingRequest, new Uint8Array()),
        );
        const ping = await this.waitForResponse(
          commandId,
          MAIN_TAG.pingResponse,
          15000,
        );
        if (ping.status !== 0) throw statusError(ping.status);
        chunksSincePing = 0;
      }

      if (!hasNext) {
        const response = await this.waitForResponse(
          commandId,
          MAIN_TAG.empty,
          15000,
        );
        if (response.status !== 0) throw statusError(response.status);
      }
    } while (offset < data.byteLength);
  }

  async md5(path: string) {
    const commandId = this.nextCommandId();
    await this.writer.write(
      encodeDelimitedMain(
        commandId,
        MAIN_TAG.storageMd5Request,
        encodeStringField(1, path),
      ),
    );
    const response = await this.waitForResponse(
      commandId,
      MAIN_TAG.storageMd5Response,
      15000,
    );
    if (response.status !== 0) throw statusError(response.status);
    const md5Bytes = parseFields(response.content ?? new Uint8Array()).find(
      (field) => field.tag === 1,
    )?.bytes;
    if (!md5Bytes) throw new Error(`CYD returned no MD5 for ${path}`);
    return textDecoder.decode(md5Bytes);
  }

  private async stopSession() {
    const commandId = this.nextCommandId();
    await this.writer.write(
      encodeDelimitedMain(commandId, MAIN_TAG.stopSession, new Uint8Array()),
    );
    const response = await this.waitForResponse(commandId, MAIN_TAG.empty, 3000);
    if (response.status !== 0) throw statusError(response.status);
    this.sessionStarted = false;
  }

  async close(graceful = true) {
    if (graceful && this.sessionStarted) {
      try {
        await this.stopSession();
      } catch {
        // Port cleanup still has to run when the device disappears mid-session.
      }
    }
    try {
      await this.reader.cancel();
    } catch {
      // The serial stream may already be closed after a device reset.
    }
    this.reader.releaseLock();
    this.writer.releaseLock();
    try {
      await this.port.close();
    } catch {
      // Closing an already disconnected CH340 is harmless.
    }
  }
}
