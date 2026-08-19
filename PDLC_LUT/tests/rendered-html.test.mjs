import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

async function render() {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);

  return worker.fetch(
    new Request("http://localhost/", {
      headers: { accept: "text/html" },
    }),
    {
      ASSETS: {
        fetch: async () => new Response("Not found", { status: 404 }),
      },
    },
    {
      waitUntil() {},
      passThroughOnException() {},
    },
  );
}

test("server-renders the PDLC LUT measurement lab", async () => {
  const response = await render();
  assert.equal(response.status, 200);
  assert.match(response.headers.get("content-type") ?? "", /^text\/html\b/i);

  const html = await response.text();
  assert.match(html, /<title>KUGLASS PDLC LUT Lab<\/title>/i);
  assert.match(html, /빛의 변화를/);
  assert.match(html, /카메라 상대 투과/);
  assert.match(html, /기준 보정/);
  assert.match(html, /LUT 결과/);
  assert.match(html, /Lab 1\.00 = controller 0\.60/);
  assert.match(html, /1\.00 · CLEAR/);
  assert.match(html, /표준 총광선투과율/);
  assert.doesNotMatch(html, /react-loading-skeleton|Your site is taking shape/i);
});

test("keeps the ESP32_A camera and control contracts explicit", async () => {
  const [serial, lab, optics, readme, packageJson] = await Promise.all([
    readFile(new URL("../lib/esp32-serial.ts", import.meta.url), "utf8"),
    readFile(new URL("../components/LutLab.tsx", import.meta.url), "utf8"),
    readFile(new URL("../lib/optics.ts", import.meta.url), "utf8"),
    readFile(new URL("../README.md", import.meta.url), "utf8"),
    readFile(new URL("../package.json", import.meta.url), "utf8"),
  ]);

  assert.match(serial, /KUGLCAM1/);
  assert.match(serial, /HEADER_BYTES = 28/);
  assert.match(serial, /fnv1a/);
  assert.match(lab, /command: "camera_stream"/);
  assert.match(lab, /command: "manual_channel"/);
  assert.match(lab, /command: "return_auto"/);
  assert.match(lab, /const LAB_MI_MAX = 1/);
  assert.match(lab, /const CONTROLLER_MI_MAX = 0\.6/);
  assert.match(lab, /target_mi: controllerMi/);
  assert.match(lab, /controller_mi = lab_mi \* 0\.60/);
  assert.match(optics, /isotonicIncreasing/);
  assert.match(readme, /표준 총광선투과율, 헤이즈 또는 Vrms가 아니다/);
  assert.match(readme, /물리 E-Stop/);
  assert.match(readme, /controller MI = Lab MI × 0\.60/);
  assert.doesNotMatch(packageJson, /drizzle|react-loading-skeleton/);
});
