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
        <span className="build-tag">WEB FLASHER · v3 · MANUAL SD</span>
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
