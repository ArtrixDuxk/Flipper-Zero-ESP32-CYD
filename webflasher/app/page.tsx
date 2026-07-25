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
            Grave o firmware.
            <br />
            <span>Sem terminal.</span>
          </h1>
          <p className="lede">
            Conecte a CYD por USB, escolha a porta CH340 e instale o Flipper Zero
            ESP32 Port diretamente pelo Chrome ou Edge.
          </p>
          <div className="spec-row" aria-label="Especificações do pacote">
            <span>ESP32 clássico</span>
            <span>4 MB flash</span>
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
          <p className="section-number">02 / PREPARO</p>
          <h2 id="guide-title">Antes de conectar</h2>
        </div>
        <ol>
          <li>
            <strong>Feche o qFlipper e o monitor serial.</strong>
            <span>A porta USB precisa estar livre para o navegador.</span>
          </li>
          <li>
            <strong>Use um cabo USB de dados.</strong>
            <span>Cabos somente de carga não mostram a porta CH340.</span>
          </li>
          <li>
            <strong>Não desconecte durante a gravação.</strong>
            <span>O processo apaga e regrava bootloader, tabela e firmware.</span>
          </li>
        </ol>
      </section>

      <footer>
        <span>Flipper Zero ESP32 Port · CYD experimental</span>
        <a
          href="https://github.com/ArtrixDuxk/Flipper-Zero-ESP32-CYD"
          target="_blank"
          rel="noreferrer"
        >
          Código-fonte ↗
        </a>
      </footer>
    </main>
  );
}
