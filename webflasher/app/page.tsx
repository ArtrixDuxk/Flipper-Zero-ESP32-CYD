import Flasher from "../components/Flasher";
import sdcardManifest from "../public/sdcard-manifest.json";

const BASE_PATH = process.env.NEXT_PUBLIC_BASE_PATH?.replace(/\/$/, "") ?? "";
const SD_CARD_DOWNLOAD = `${BASE_PATH}/downloads/flipper-cyd-sd-card.zip`;
const SD_CARD_SIZE = `${(sdcardManifest.totalSize / 1024 / 1024).toFixed(1)} MB`;

export default function Home() {
  return (
    <main>
      <header className="site-header">
        <a className="brand" href="#top" aria-label="Flipper CYD Web Flasher">
          <span className="brand-mark" aria-hidden="true">
            CYD
          </span>
          <span>Flipper ESP32</span>
        </a>
        <div className="header-actions">
          <span className="build-tag">WEB FLASHER · v3.1 · MANUAL SD</span>
          <a
            className="github-star"
            href="https://github.com/ArtrixDuxk/Flipper-Zero-ESP32-CYD"
            target="_blank"
            rel="noreferrer"
            aria-label="Star Flipper Zero ESP32 CYD on GitHub"
          >
            <span aria-hidden="true">★</span>
            Star on GitHub
          </a>
        </div>
      </header>

      <section className="hero" id="top">
        <div className="hero-copy">
          <p className="eyebrow">ESP32-2432S028 + NM-RF-HAT</p>
          <h1>
            Flash the firmware.
            <br />
            <span>No terminal.</span>
          </h1>
          <p className="lede">
            Connect your CYD over USB and install the Flipper Zero ESP32 Port
            directly from Chrome or Edge. The flasher writes only the firmware;
            the SD card package is available below.
          </p>
          <div className="spec-row" aria-label="Firmware package specifications">
            <span>Classic ESP32</span>
            <span>4 MB Flash</span>
            <span>DIO · 40 MHz</span>
          </div>
        </div>

        <div className="board-visual" aria-hidden="true">
          <div className="antenna" />
          <div className="board">
            <div className="screen">
              <span className="screen-title">FLIPPER</span>
              <span className="screen-subtitle">ESP32 PORT</span>
              <div className="screen-wave">
                <i />
                <i />
                <i />
                <i />
                <i />
              </div>
            </div>
            <div className="board-label">ESP32-2432S028</div>
            <div className="chip">ESP32</div>
            <div className="usb-port" />
          </div>
        </div>
      </section>

      <a className="compatibility-alert" href="#compatibility">
        <span aria-hidden="true">!</span>
        <div>
          <strong>Experimental CYD port</strong>
          <p>
            Bluetooth is still in development. Native USB features and some
            memory-heavy apps are not supported. Review the compatibility notes
            before flashing.
          </p>
        </div>
        <b aria-hidden="true">↓</b>
      </a>

      <Flasher />

      <section className="sd-guide" aria-labelledby="sd-guide-title">
        <div className="sd-guide-heading">
          <div>
            <p className="section-number">02 / SD CARD</p>
            <h2 id="sd-guide-title">Prepare the SD card manually</h2>
            <p>
              The firmware needs its resource folders on a FAT32 microSD card.
              Download the package, then copy its contents to the card yourself.
            </p>
          </div>
          <div className="sd-download-card">
            <span>CYD SD RESOURCE PACK</span>
            <strong>
              {sdcardManifest.files.length} files · {SD_CARD_SIZE}
            </strong>
            <a href={SD_CARD_DOWNLOAD} download>
              Download SD card files
              <b aria-hidden="true">↓</b>
            </a>
          </div>
        </div>

        <ol className="sd-steps">
          <li>
            <span>01</span>
            <div>
              <strong>Format the microSD card as FAT32.</strong>
              <p>Back up anything important first, because formatting erases it.</p>
            </div>
          </li>
          <li>
            <span>02</span>
            <div>
              <strong>Download and extract the ZIP file.</strong>
              <p>Do not copy the ZIP itself to the card.</p>
            </div>
          </li>
          <li>
            <span>03</span>
            <div>
              <strong>Copy everything inside the ZIP to the SD card root.</strong>
              <p>
                Folders such as <code>subghz</code>, <code>nfc</code>, and{" "}
                <code>wifi</code> must be directly at the top level.
              </p>
            </div>
          </li>
          <li>
            <span>04</span>
            <div>
              <strong>Safely eject the card and insert it into the powered-off CYD.</strong>
              <p>Power the board back on after the card is fully seated.</p>
            </div>
          </li>
        </ol>
      </section>

      <section className="guide" aria-labelledby="guide-title">
        <div>
          <p className="section-number">03 / BEFORE FLASHING</p>
          <h2 id="guide-title">Before you connect</h2>
        </div>
        <ol>
          <li>
            <strong>Close any serial monitor.</strong>
            <span>The USB port must be available to your browser.</span>
          </li>
          <li>
            <strong>Use a USB data cable.</strong>
            <span>Charge-only cables cannot communicate with the ESP32.</span>
          </li>
          <li>
            <strong>Keep the board connected while flashing.</strong>
            <span>Wait for the completion message before unplugging the cable.</span>
          </li>
        </ol>
      </section>

      <section
        className="compatibility"
        id="compatibility"
        aria-labelledby="compatibility-title"
      >
        <div className="compatibility-heading">
          <div>
            <p className="section-number">04 / SUPPORT STATUS</p>
            <h2 id="compatibility-title">Know the limitations</h2>
          </div>
          <p>
            This port runs on a classic ESP32 with 4 MB flash and no PSRAM. It
            cannot provide every feature available on a real Flipper Zero or a
            larger ESP32-S3 board.
          </p>
        </div>

        <div className="support-grid">
          <article className="support-card support-unavailable">
            <span>NOT SUPPORTED</span>
            <h3>Native USB features</h3>
            <ul>
              <li>USB Mass Storage is unavailable.</li>
              <li>BadUSB over USB is unavailable.</li>
              <li>The CYD exposes a CH340 serial adapter, not native USB-OTG.</li>
            </ul>
          </article>

          <article className="support-card support-experimental">
            <span>IN DEVELOPMENT</span>
            <h3>Bluetooth and BLE</h3>
            <ul>
              <li>The CYD hardware supports Bluetooth and BLE.</li>
              <li>Bluetooth is currently disabled while its low-memory integration is being developed.</li>
              <li>BLE Spam, BLE Walk, BLE HID, and BLE BadUSB are not ready yet.</li>
            </ul>
          </article>

          <article className="support-card support-required">
            <span>FORK REQUIRED</span>
            <h3>qFlipper desktop app</h3>
            <ul>
              <li>Stock qFlipper discovery does not support the CYD.</li>
              <li>Use the qFlipper fork bundled with this repository.</li>
              <li>Communication runs through UART0 and the CH340 adapter.</li>
            </ul>
          </article>

          <article className="support-card support-experimental">
            <span>EXPERIMENTAL</span>
            <h3>Wi-Fi and heavy apps</h3>
            <ul>
              <li>Wi-Fi scanning and supported tools work within tight RAM limits.</li>
              <li>Monitor mode, handshake capture, and heavy views may be unstable.</li>
              <li>Running concurrent radio-heavy features is not recommended.</li>
            </ul>
          </article>

          <article className="support-card support-limited">
            <span>LIMITED</span>
            <h3>Apps and scripting</h3>
            <ul>
              <li>Doom and Wolf3D are excluded because they require PSRAM.</li>
              <li>Snake touch controls still need broader hardware testing.</li>
              <li>
                JavaScript modules for serial, GPIO, I2C, and SPI are unavailable.
              </li>
            </ul>
          </article>

          <article className="support-card support-limited">
            <span>HARDWARE LIMIT</span>
            <h3>NM-RF-HAT radio switch</h3>
            <ul>
              <li>Only one HAT peripheral can be active at a time.</li>
              <li>
                DIP positions: 1 CC1101, 2 nRF24, 3 NFC, 4 IR, and 5 RF433.
              </li>
              <li>Encrypted Sub-GHz manufacturer keystores cannot be decrypted.</li>
            </ul>
          </article>
        </div>
      </section>

      <footer>
        <span>Flipper Zero ESP32 Port · Experimental CYD build</span>
        <a
          href="https://github.com/ArtrixDuxk/Flipper-Zero-ESP32-CYD"
          target="_blank"
          rel="noreferrer"
        >
          Source code ↗
        </a>
      </footer>
    </main>
  );
}
