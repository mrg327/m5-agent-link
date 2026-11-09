#!/usr/bin/env node
// Local design-board proof of concept: one process that is both
//   - an MCP stdio server the Claude Code session loads as a *channel*, so a press on the page is
//     pushed into the session as a <channel source="board"> message (no polling), and
//   - a small HTTP server for the page (GET /), live updates (GET /events, server-sent events) and
//     presses (POST /press).
// The session answers through the board_reply tool, which the page shows live.
// No dependencies: MCP over stdio is newline-delimited JSON-RPC, written by hand here.
//
// Run by Claude Code (see README.md):  claude --mcp-config tools/board/mcp.json \
//     --dangerously-load-development-channels server:board
// Env: BOARD_PORT (7880), BOARD_HOST (127.0.0.1; comma-separated list allowed),
//      BOARD_TAILSCALE (default 1) also listens on this machine's Tailscale IPv4 (tailnet only,
//      never 0.0.0.0); set BOARD_TAILSCALE=0 for localhost only.

import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { fileURLToPath } from "node:url";
import { execFileSync, spawn } from "node:child_process";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const PORT = Number(process.env.BOARD_PORT || 7880);
const HOSTS = (process.env.BOARD_HOST || "127.0.0.1").split(",").map((h) => h.trim()).filter(Boolean);
if (process.env.BOARD_TAILSCALE !== "0") {
  try {
    const ip = execFileSync("tailscale", ["ip", "-4"], { encoding: "utf8", timeout: 3000 }).trim().split("\n")[0];
    if (/^100\.\d+\.\d+\.\d+$/.test(ip) && !HOSTS.includes(ip)) HOSTS.push(ip);
  } catch { /* no tailscale: localhost only */ }
}
const HOST = HOSTS[0];
const LOG_DIR = path.join(os.homedir(), ".agentlink");
const LOG = path.join(LOG_DIR, "board-log.jsonl");

const log = (...a) => process.stderr.write("[board] " + a.join(" ") + "\n"); // stdout is the MCP pipe
const record = (ev) => {
  try { fs.mkdirSync(LOG_DIR, { recursive: true }); fs.appendFileSync(LOG, JSON.stringify(ev) + "\n"); } catch {}
};

// ── state ──
const presses = new Map(); // id -> {id, n, kind, text, sent, pushed, answered, reply, replies:[]}
let nextN = 1;
let session = { connected: false, since: 0 };
const clients = new Set();
const broadcast = (type, data) => {
  const msg = `event: ${type}\ndata: ${JSON.stringify(data)}\n\n`;
  for (const res of clients) res.write(msg);
};

// ── MCP over stdio ──
const INSTRUCTIONS = `Events from the m5-agent-link design board arrive as <channel source="board" id="..." kind="...">.
They come from the user pressing buttons on a local page (http://${HOST}:${PORT}); the page shows your answers.
Always answer each event with the board_reply tool, passing its id. Keep replies short and plain.
- kind="ping": reply at once with a one-line acknowledgement. Do nothing else.
- kind="say": the body is a message the user typed. Answer it as you would in chat, in board_reply.
- kind="agent": reply "started", start one background agent (Agent tool, run_in_background) that runs
  the simulator tests (ctest in /home/matt/projects/m5-agent-link-ui-v2/simulator/build, reconfigure if
  needed, change no files), then when it reports, call board_reply again with the pass count.
If several events arrive together, answer every one of them.`;

const TOOLS = [
  {
    name: "board_reply",
    description: "Show a reply on the design board for one event (the id from its <channel> tag). Call it once per event; call again to add a follow-up (e.g. when a background job finishes).",
    inputSchema: {
      type: "object",
      properties: { id: { type: "string", description: "The event id from the channel tag" }, text: { type: "string" } },
      required: ["id", "text"],
    },
  },
];

const send = (msg) => process.stdout.write(JSON.stringify(msg) + "\n");
const result = (id, r) => send({ jsonrpc: "2.0", id, result: r });
const error = (id, code, message) => send({ jsonrpc: "2.0", id, error: { code, message } });

// Claude Code cannot deliver channel messages over MCP revision 2026-07-28 (docs: mcp.md), so never
// agree to that one; any older revision the client offers is fine.
const SUPPORTED = ["2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"];

function onRpc(m) {
  if (m.method === "initialize") {
    const want = m.params?.protocolVersion;
    result(m.id, {
      protocolVersion: SUPPORTED.includes(want) ? want : SUPPORTED[0],
      capabilities: { experimental: { "claude/channel": {} }, tools: {} },
      serverInfo: { name: "board", version: "0.1.0" },
      instructions: INSTRUCTIONS,
    });
    session = { connected: true, since: Date.now(), client: m.params?.clientInfo?.name || "?" };
    broadcast("session", session);
    log("initialize from", session.client, "protocol", want);
    return;
  }
  if (m.method === "notifications/initialized" || m.method?.startsWith("notifications/")) return;
  if (m.method === "ping") return result(m.id, {});
  if (m.method === "tools/list") return result(m.id, { tools: TOOLS });
  if (m.method === "tools/call") {
    const { name, arguments: args = {} } = m.params || {};
    if (name !== "board_reply") return error(m.id, -32602, "unknown tool " + name);
    const p = presses.get(String(args.id));
    if (!p) return result(m.id, { content: [{ type: "text", text: `no event with id ${args.id}` }], isError: true });
    const now = Date.now();
    if (!p.answered) p.answered = now;
    p.replies.push({ at: now, text: String(args.text || "").slice(0, 4000) });
    record({ ev: "reply", id: p.id, at: now, ms_from_press: now - p.sent });
    broadcast("press", p);
    return result(m.id, { content: [{ type: "text", text: "shown on the board" }] });
  }
  if (m.id !== undefined) error(m.id, -32601, "method not found: " + m.method);
}

let buf = "";
process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => {
  buf += chunk;
  let i;
  while ((i = buf.indexOf("\n")) >= 0) {
    const line = buf.slice(0, i).trim();
    buf = buf.slice(i + 1);
    if (!line) continue;
    try { onRpc(JSON.parse(line)); } catch (e) { log("bad rpc line:", e.message); }
  }
});
process.stdin.on("end", () => { log("session closed stdin; exiting"); process.exit(0); });

// ── HTTP ──
// Read per request, so page edits show on reload without restarting the session's MCP server.
const page = () => fs.readFileSync(path.join(HERE, "board.html"), "utf8");

// ── gallery (B1) ──
const ROOT = path.resolve(HERE, "../..");
const SHOTS = path.join(ROOT, "build", "board");
const STATES = path.join(ROOT, "design", "board", "states.json");
const readJson_ = (f, d) => { try { return JSON.parse(fs.readFileSync(f, "utf8")); } catch { return d; } };
let shaCache = { at: 0, sha: "" };
function currentSha() {
  if (Date.now() - shaCache.at < 5000) return shaCache.sha;
  try {
    shaCache = { at: Date.now(), sha: execFileSync("node", [path.join(HERE, "render.mjs"), "--sha"], { encoding: "utf8", timeout: 5000 }).trim() };
  } catch { shaCache = { at: Date.now(), sha: "" }; }
  return shaCache.sha;
}
function gallery() {
  const latest = readJson_(path.join(SHOTS, "latest.json"), null);
  const index = latest ? readJson_(path.join(SHOTS, latest.sha, "index.json"), null) : null;
  return { current: currentSha(), index, states: readJson_(STATES, { groups: [] }), rendering: !!renderJob };
}
let renderJob = null;
function startRender() {
  if (renderJob) return false;
  renderJob = spawn("node", [path.join(HERE, "render.mjs")], { cwd: ROOT });
  let buf2 = "";
  renderJob.stdout.on("data", (d) => {
    buf2 += d;
    let i;
    while ((i = buf2.indexOf("\n")) >= 0) {
      const line = buf2.slice(0, i); buf2 = buf2.slice(i + 1);
      try { broadcast("render", JSON.parse(line)); } catch {}
    }
  });
  renderJob.on("close", () => { renderJob = null; shaCache.at = 0; broadcast("gallery", gallery()); });
  return true;
}

function readJson(req) {
  return new Promise((resolve) => {
    let s = "";
    req.on("data", (c) => { s += c; if (s.length > 16384) req.destroy(); });
    req.on("end", () => { try { resolve(JSON.parse(s || "{}")); } catch { resolve({}); } });
  });
}

const handler = async (req, res) => {
  // Only same-origin pages may press: refuse cross-site form posts from other tabs.
  const origin = req.headers.origin;
  if (req.method === "POST" && origin && origin !== `http://${req.headers.host}`) {
    res.writeHead(403).end("cross-origin");
    return;
  }
  if (req.method === "GET" && req.url === "/") {
    res.writeHead(200, { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" }).end(page());
    return;
  }
  if (req.method === "GET" && req.url === "/api/gallery") {
    res.writeHead(200, { "content-type": "application/json", "cache-control": "no-store" }).end(JSON.stringify(gallery()));
    return;
  }
  const shot = req.method === "GET" && req.url.match(/^\/shots\/([0-9a-f]{4,40}(?:-dirty)?)\/([a-z0-9_]{1,64})\.png$/);
  if (shot) {
    fs.readFile(path.join(SHOTS, shot[1], shot[2] + ".png"), (err, data) => {
      if (err) res.writeHead(404).end("no such shot");
      else res.writeHead(200, { "content-type": "image/png", "cache-control": "max-age=31536000, immutable" }).end(data);
    });
    return;
  }
  if (req.method === "POST" && req.url === "/render") {
    const started = startRender();
    res.writeHead(started ? 202 : 409, { "content-type": "application/json" }).end(JSON.stringify({ started }));
    return;
  }
  if (req.method === "GET" && req.url === "/events") {
    res.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-store", connection: "keep-alive" });
    res.write(`event: session\ndata: ${JSON.stringify(session)}\n\n`);
    for (const p of [...presses.values()].slice(-30)) res.write(`event: press\ndata: ${JSON.stringify(p)}\n\n`);
    clients.add(res);
    req.on("close", () => clients.delete(res));
    return;
  }
  if (req.method === "POST" && req.url === "/press") {
    const body = await readJson(req);
    const kind = ["ping", "say", "agent"].includes(body.kind) ? body.kind : "ping";
    const text = String(body.text || "").slice(0, 2000);
    const sent = Number(body.sent) || Date.now(); // the page's clock (same machine on localhost)
    const id = "b" + Date.now().toString(36);
    const p = { id, n: nextN++, kind, text, dev: String(body.dev || ""), sent, pushed: 0, answered: 0, replies: [] };
    presses.set(id, p);
    if (session.connected) {
      send({
        jsonrpc: "2.0",
        method: "notifications/claude/channel",
        params: { content: kind === "say" ? text : `${kind} from the board (${p.dev || "browser"})`, meta: { id, kind } },
      });
      p.pushed = Date.now();
    }
    record({ ev: "press", id, kind, sent, pushed: p.pushed, dev: p.dev });
    broadcast("press", p);
    res.writeHead(200, { "content-type": "application/json" }).end(JSON.stringify({ id, pushed: p.pushed, session: session.connected }));
    return;
  }
  res.writeHead(404).end("not found");
};
// A reconnect can start this process while the previous copy still holds the port: keep retrying
// instead of running headless (pushes would then go to a session no page can reach).
function listenOn(h) {
  const srv = http.createServer(handler);
  srv.on("error", (e) => {
    log(`http error on ${h}:`, e.message);
    if (e.code === "EADDRINUSE") setTimeout(() => listenOn(h), 2000);
  });
  srv.listen(PORT, h, () => log(`board on http://${h}:${PORT}`));
}
for (const h of HOSTS) listenOn(h);
