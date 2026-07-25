import Flasher from "../components/Flasher";

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
        <span className="build-tag">WEB FLASHER · v1</span>
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
            Connect your CYD over USB, select its CH340 port, and install the
            Flipper Zero ESP32 Port plus its required SD card resources directly
            from Chrome or Edge.
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

      <section className="guide" aria-labelledby="guide-title">
        <div>
          <p className="section-number">02 / PREPARE</p>
          <h2 id="guide-title">Before you connect</h2>
        </div>
        <ol>
          <li>
            <strong>Insert a FAT32 microSD card.</strong>
            <span>The installer writes the required resource pack after flashing.</span>
          </li>
          <li>
            <strong>Close any serial monitor.</strong>
            <span>The USB port must be available to your browser.</span>
          </li>
          <li>
            <strong>Use a USB data cable and keep it connected.</strong>
            <span>The process flashes the board, restarts it, and provisions the SD card.</span>
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
