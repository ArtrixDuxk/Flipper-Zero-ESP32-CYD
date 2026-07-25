import type {Metadata} from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Flipper CYD Web Flasher",
  description:
    "Instale o Flipper Zero ESP32 Port na ESP32-2432S028 CYD diretamente pelo navegador.",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="pt-BR">
      <body>{children}</body>
    </html>
  );
}
