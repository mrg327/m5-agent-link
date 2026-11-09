#!/usr/bin/env node
// Drive the board page in headless Chrome over the DevTools protocol, for checks and screenshots.
// (Chrome's --screenshot/--dump-dom flags fire before the page's fetches and events land.) Node 18 has
// no WebSocket global, so this speaks the minimal client side of RFC 6455 itself.
//   CH=$(command -v google-chrome) node tools/board/drive.mjs http://127.0.0.1:7881/ /tmp/out [steps.json]
// steps: [{"wait":ms} | {"eval":"js"} | {"tap":[x,y]} | {"shot":"name"}] (default: wait, shot "page")
import { spawn } from "node:child_process";
import http from "node:http";
import crypto from "node:crypto";
import fs from "node:fs";
const [url, prefix, stepsFile] = process.argv.slice(2);
const steps = stepsFile ? JSON.parse(fs.readFileSync(stepsFile, "utf8")) : [{ wait: 2500 }, { shot: "page" }];
const chrome = spawn(process.env.CH, ["--headless=new", "--no-sandbox", "--user-data-dir=" + (process.env.UDD || fs.mkdtempSync("/tmp/board-chrome-")), "--remote-debugging-port=9333", "about:blank"], { stdio: "ignore" });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let target;
for (let i = 0; i < 60 && !target; i++) { await sleep(250); try { target = (await (await fetch("http://127.0.0.1:9333/json")).json()).find((x) => x.type === "page"); } catch {} }
const sock = await new Promise((res, rej) => {
  const u = new URL(target.webSocketDebuggerUrl);
  const req = http.request({ host: u.hostname, port: u.port, path: u.pathname, headers: { Connection: "Upgrade", Upgrade: "websocket", "Sec-WebSocket-Version": "13", "Sec-WebSocket-Key": crypto.randomBytes(16).toString("base64") } });
  req.on("upgrade", (r, s) => res(s)); req.on("error", rej); req.end();
});
const sendFrame = (str) => {
  const p = Buffer.from(str), mask = crypto.randomBytes(4);
  let h; if (p.length < 126) h = Buffer.from([0x81, 0x80 | p.length]);
  else if (p.length < 65536) { h = Buffer.alloc(4); h[0] = 0x81; h[1] = 0x80 | 126; h.writeUInt16BE(p.length, 2); }
  else { h = Buffer.alloc(10); h[0] = 0x81; h[1] = 0x80 | 127; h.writeBigUInt64BE(BigInt(p.length), 2); }
  for (let i = 0; i < p.length; i++) p[i] ^= mask[i & 3];
  sock.write(Buffer.concat([h, mask, p]));
};
let buf = Buffer.alloc(0), msgParts = [];
const waits = new Map(); let id = 0;
sock.on("data", (d) => {
  buf = Buffer.concat([buf, d]);
  for (;;) {
    if (buf.length < 2) return;
    let len = buf[1] & 127, off = 2;
    if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); off = 4; }
    else if (len === 127) { if (buf.length < 10) return; len = Number(buf.readBigUInt64BE(2)); off = 10; }
    if (buf.length < off + len) return;
    const fin = buf[0] & 0x80; msgParts.push(buf.subarray(off, off + len)); buf = buf.subarray(off + len);
    if (!fin) continue;
    const m = JSON.parse(Buffer.concat(msgParts).toString()); msgParts = [];
    if (m.id && waits.has(m.id)) { waits.get(m.id)(m); waits.delete(m.id); }
    else if (m.method === "Runtime.exceptionThrown") console.log("EXCEPTION", m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text);
    else if (m.method === "Runtime.consoleAPICalled") console.log("CONSOLE", m.params.args.map((a) => a.value ?? a.description).join(" "));
  }
});
const cmd = (method, params = {}) => new Promise((r) => { const i = ++id; waits.set(i, r); sendFrame(JSON.stringify({ id: i, method, params })); });
await cmd("Runtime.enable");
await cmd("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
await cmd("Emulation.setTouchEmulationEnabled", { enabled: true });
await cmd("Page.navigate", { url });
for (const s of steps) {
  if (s.wait) await sleep(s.wait);
  if (s.eval) { const r = await cmd("Runtime.evaluate", { expression: s.eval, awaitPromise: true, returnByValue: true }); console.log("EVAL", JSON.stringify(r.result?.result?.value ?? r.result?.exceptionDetails?.exception?.description)); }
  if (s.tap) { for (const type of ["mousePressed", "mouseReleased"]) await cmd("Input.dispatchMouseEvent", { type, x: s.tap[0], y: s.tap[1], button: "left", clickCount: 1 }); }
  if (s.shot) { const r = await cmd("Page.captureScreenshot", { format: "png" }); fs.writeFileSync(`${prefix}-${s.shot}.png`, Buffer.from(r.result.data, "base64")); console.log("SHOT", `${prefix}-${s.shot}.png`); }
}
chrome.kill(); process.exit(0);
