import type {Metadata} from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Flipper CYD Web Flasher",
  description:
    "Install the Flipper Zero ESP32 Port on an ESP32-2432S028 CYD directly from your browser.",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
