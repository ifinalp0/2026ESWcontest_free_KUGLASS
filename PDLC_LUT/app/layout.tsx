import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "KUGLASS PDLC LUT Lab",
  description: "OV2640 기반 PDLC 상대 광응답 측정 및 LUT 추정 도구",
  openGraph: {
    title: "KUGLASS PDLC LUT Lab",
    description: "ESP32_A 카메라로 PDLC의 MI별 상대 광응답을 기록하고 LUT 후보를 만듭니다.",
    images: [{ url: "/og-card.png", width: 1731, height: 909 }],
  },
  twitter: {
    card: "summary_large_image",
    images: ["/og-card.png"],
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="ko">
      <body>{children}</body>
    </html>
  );
}
