#!/usr/bin/env node
// Board web server and tool handlers. board.mjs (the MCP shim the Claude Code session loads) forks
// this file and restarts it whenever it changes on disk, so server edits need no /mcp reconnect.
// Open pages reload themselves when board.html changes. The two processes talk over Node IPC:
//   shim -> server: {t:"session", session} | {t:"call", rid, name, args} | {t:"tools", rid}
//   server -> shim: {t:"ready"} | {t:"push", params} | {t:"result", rid, result}
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
const ROOT = path.resolve(HERE, "../..");
const PORT = Number(process.env.BOARD_PORT || 7880);
const HOSTS = (process.env.BOARD_HOST || "127.0.0.1").split(",").map((h) => h.trim()).filter(Boolean);
if (process.env.BOARD_TAILSCALE !== "0") {
  try {
    const ip = execFileSync("tailscale", ["ip", "-4"], { encoding: "utf8", timeout: 3000 }).trim().split("\n")[0];
    if (/^100\.\d+\.\d+\.\d+$/.test(ip) && !HOSTS.includes(ip)) HOSTS.push(ip);
  } catch { /* no tailscale: localhost only */ }
}
const LOG_DIR = path.join(os.homedir(), ".agentlink");
const LOG = path.join(LOG_DIR, "board-log.jsonl");
const BOOT = Date.now().toString(36);

const log = (...a) => process.stderr.write("[board/server] " + a.join(" ") + "\n");
const record = (ev) => {
  try { fs.mkdirSync(LOG_DIR, { recursive: true }); fs.appendFileSync(LOG, JSON.stringify(ev) + "\n"); } catch {}
};
const readJson_ = (f, d) => { try { return JSON.parse(fs.readFileSync(f, "utf8")); } catch { return d; } };
const toShim = (m) => { if (process.send) process.send(m); };
const push = (content, meta) => toShim({ t: "push", params: { content, meta } });

// ── live updates ──
const clients = new Set();
const broadcast = (type, data) => {
  const msg = `event: ${type}\ndata: ${JSON.stringify(data)}\n\n`;
  for (const res of clients) res.write(msg);
};
let session = { connected: false, since: 0 };

// ── presses (Talk tab); kept on disk so a server restart doesn't orphan an unanswered event ──
const SHOTS = path.join(ROOT, "build", "board");
const PRESSES = path.join(SHOTS, "presses.json");
const presses = new Map(readJson_(PRESSES, []).map((p) => [p.id, p]));
let nextN = Math.max(0, ...[...presses.values()].map((p) => p.n || 0)) + 1;
function savePresses() {
  try {
    fs.mkdirSync(SHOTS, { recursive: true });
    fs.writeFileSync(PRESSES, JSON.stringify([...presses.values()].slice(-200)));
  } catch {}
}
function addPress(p) { presses.set(p.id, p); savePresses(); broadcast("press", p); }

// ── gallery (B1) ──
const STATES = path.join(ROOT, "design", "board", "states.json");
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
const LOCK = path.join(SHOTS, "render.lock");
const pidAlive = (pid) => { try { process.kill(pid, 0); return true; } catch (e) { return e.code === "EPERM"; } };
function renderLocked() {
  let pid;
  try { pid = Number(fs.readFileSync(LOCK, "utf8").trim()); } catch { return false; }
  if (pid > 0 && pidAlive(pid)) return true;
  try { fs.unlinkSync(LOCK); } catch {}
  return false;
}
function stopRender() {
  if (!renderJob) return;
  // render.mjs runs in its own process group, so this also reaches cmake and the simulators it started
  try { process.kill(-renderJob.pid, "SIGTERM"); } catch { try { renderJob.kill("SIGTERM"); } catch {} }
}
function startRender() {
  if (renderJob || renderLocked()) return false;
  const job = spawn("node", [path.join(HERE, "render.mjs")], { cwd: ROOT, detached: true });
  if (!job.pid) return false;
  renderJob = job;
  try {
    fs.mkdirSync(SHOTS, { recursive: true });
    fs.writeFileSync(LOCK, String(job.pid));
  } catch (e) { log("could not write render lock:", e.message); }
  job.on("error", (e) => log("render spawn failed:", e.message));
  let buf = "";
  job.stdout.on("data", (d) => {
    buf += d;
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
      const line = buf.slice(0, i); buf = buf.slice(i + 1);
      try { broadcast("render", JSON.parse(line)); } catch {}
    }
  });
  job.on("close", () => {
    renderJob = null;
    try { if (Number(fs.readFileSync(LOCK, "utf8")) === job.pid) fs.unlinkSync(LOCK); } catch {}
    shaCache.at = 0;
    broadcast("gallery", gallery());
  });
  return true;
}

// ── comments (B2) ──
// Pins on screenshots, one JSON object per line in design/board/comments.jsonl (committed with the
// branch). Lifecycle: draft -> sent (one channel push per batch) -> seen (board_seen) -> answered.
const COMMENTS = path.join(ROOT, "design", "board", "comments.jsonl");
const comments = new Map();
let nextC = 1;
try {
  for (const line of fs.readFileSync(COMMENTS, "utf8").split("\n")) {
    if (!line.trim()) continue;
    try { const c = JSON.parse(line); comments.set(c.id, c); nextC = Math.max(nextC, (c.n || 0) + 1); } catch {}
  }
} catch { /* none yet */ }
const commentList = () => [...comments.values()];
function saveComments() {
  let ok = true;
  try {
    fs.mkdirSync(path.dirname(COMMENTS), { recursive: true });
    const tmp = COMMENTS + ".tmp";
    fs.writeFileSync(tmp, commentList().map((c) => JSON.stringify(c)).join("\n") + (comments.size ? "\n" : ""));
    fs.renameSync(tmp, COMMENTS);
  } catch (e) { ok = false; saveError = e.message; log("could not save comments:", e.message); }
  broadcast("comments", commentList()); // always the in-memory state; failed saves are rolled back by the caller where possible
  return ok;
}
let saveError = "";
const SHA_RE = /^[0-9a-f]{4,40}(?:-dirty)?$/, SHOT_RE = /^[a-z0-9_]{1,64}$/;
function sendBatch(dev) {
  const drafts = commentList().filter((c) => !c.sent);
  if (!drafts.length) return { error: "no drafts" };
  if (!session.connected) return { error: "no session" };
  const id = "k" + Date.now().toString(36);
  const now = Date.now();
  const labels = {};
  for (const g of readJson_(STATES, { groups: [] }).groups || []) Object.assign(labels, g.shots);
  const lines = drafts.map((c) => [
    `[${c.id}] ${c.shot}${labels[c.shot] ? ` "${labels[c.shot]}"` : ""} · build ${c.sha} · pin at (${c.x}, ${c.y}) of 466x466`,
    `  image: ${path.join(SHOTS, c.sha, c.shot + ".png")}`,
    `  note: ${c.text.replace(/\n/g, "\n        ")}`,
  ].join("\n"));
  push(`${drafts.length} note${drafts.length > 1 ? "s" : ""} pinned on the board (${dev || "browser"}):\n\n${lines.join("\n\n")}`,
    { id, kind: "comments", notes: drafts.map((c) => c.id).join(",") });
  for (const c of drafts) { c.sent = now; c.batch = id; }
  addPress({ id, n: nextN++, kind: "comments", text: drafts.map((c) => c.id).join(", "), dev, sent: now, pushed: now, answered: 0, replies: [] });
  record({ ev: "comments", id, notes: drafts.length, dev });
  if (!saveComments()) {
    // The channel push already happened, so keep the batch marked sent: unsending it would duplicate it.
    log("batch", id, "was pushed but the notes could not be saved:", saveError);
    return { error: `batch sent to the session but could not save notes: ${saveError}`, id, notes: drafts.length, pushed: true };
  }
  return { id, notes: drafts.length };
}

// ── tools (the shim forwards tools/list and tools/call here) ──
const TOOLS = [
  {
    name: "board_reply",
    description: "Show a reply on the design board for one event or pinned note (the id from its <channel> tag, or a note id like c12). Call again to add a follow-up (e.g. when a background job finishes).",
    inputSchema: {
      type: "object",
      properties: { id: { type: "string", description: "The event id from the channel tag, or a note id" }, text: { type: "string" } },
      required: ["id", "text"],
    },
  },
  {
    name: "board_seen",
    description: "Mark pinned notes as seen on the board (ids like c12 from a comments batch). Call it as soon as a batch arrives, before working on the notes.",
    inputSchema: {
      type: "object",
      properties: { ids: { type: "array", items: { type: "string" } } },
      required: ["ids"],
    },
  },
];
const text = (t, isError) => ({ content: [{ type: "text", text: t }], ...(isError ? { isError: true } : {}) });
function callTool(name, args = {}) {
  const now = Date.now();
  if (name === "board_seen") {
    const hit = [];
    for (const id of [].concat(args.ids || [])) {
      const c = comments.get(String(id));
      if (c && c.sent) { if (!c.seen) c.seen = now; hit.push(c.id); }
    }
    if (hit.length && !saveComments()) return text(`marked seen in memory but could not save: ${saveError}`, true);
    return text(hit.length ? `marked seen: ${hit.join(", ")}` : "no sent notes with those ids", !hit.length);
  }
  if (name !== "board_reply") return text("unknown tool " + name, true);
  const reply = { at: now, text: String(args.text || "").slice(0, 4000) };
  const c = comments.get(String(args.id));
  if (c) {
    if (!c.seen) c.seen = now;
    if (!c.answered) c.answered = now;
    c.replies.push(reply);
    record({ ev: "note_reply", id: c.id, at: now, ms_from_send: now - c.sent });
    if (!saveComments()) return text(`reply shown on the board but could not save: ${saveError}`, true);
    return text("shown on the note");
  }
  const p = presses.get(String(args.id));
  if (!p) return text(`no event or note with id ${args.id}`, true);
  if (!p.answered) p.answered = now;
  p.replies.push(reply);
  if (p.kind === "comments") { // a reply to the whole batch marks its notes seen
    for (const c2 of comments.values()) if (c2.batch === p.id && !c2.seen) c2.seen = now;
    if (!saveComments()) log("could not save notes marked seen:", saveError);
  }
  record({ ev: "reply", id: p.id, at: now, ms_from_press: now - p.sent });
  addPress(p);
  return text("shown on the board");
}

process.on("message", (m) => {
  if (m.t === "session") { session = m.session; broadcast("session", session); }
  else if (m.t === "tools") toShim({ t: "result", rid: m.rid, result: { tools: TOOLS } });
  else if (m.t === "call") {
    let r;
    try { r = callTool(m.name, m.args); } catch (e) { r = text("board error: " + e.message, true); }
    toShim({ t: "result", rid: m.rid, result: r });
  }
});
const servers = [];
let closing = false;
function shutdown() {
  if (closing) return;
  closing = true;
  stopRender();
  for (const res of clients) { try { res.end(); } catch {} }
  for (const srv of servers) { try { srv.close(); srv.closeAllConnections?.(); } catch {} }
  const done = () => process.exit(0);
  if (renderJob) { renderJob.once("close", done); setTimeout(() => { try { process.kill(-renderJob.pid, "SIGKILL"); } catch {} done(); }, 3000).unref(); }
  else setTimeout(done, 50);
}
process.on("disconnect", shutdown); // the shim went away
process.on("SIGTERM", shutdown);
process.on("SIGINT", shutdown);

// ── page version: open pages reload when board.html changes ──
const PAGE = path.join(HERE, "board.html");
const page = () => fs.readFileSync(PAGE, "utf8");
const pageVersion = () => { try { return String(fs.statSync(PAGE).mtimeMs); } catch { return "0"; } };
const hello = () => ({ boot: BOOT, page: pageVersion() });
let helloTimer = null;
fs.watch(HERE, (ev, f) => {
  if (f !== "board.html") return;
  clearTimeout(helloTimer);
  helloTimer = setTimeout(() => broadcast("hello", hello()), 250);
});

// ── HTTP ──
// Resolves the parsed body, {} for an empty or invalid one, or null when the body was too big or the
// connection broke before it ended.
function readJson(req) {
  return new Promise((resolve) => {
    let s = "", over = false;
    const finish = (v) => resolve(v);
    req.on("data", (c) => {
      if (over) return;
      s += c;
      if (s.length > 16384) { over = true; s = ""; finish(null); req.resume(); }
    });
    req.on("end", () => { if (!over) { try { finish(JSON.parse(s || "{}")); } catch { finish({}); } } });
    req.on("close", () => finish(null));
    req.on("error", () => finish(null));
  });
}

const handler = async (req, res) => {
  const tooBig = () => res.writeHead(413, { "content-type": "application/json", connection: "close" }).end(JSON.stringify({ error: "request body too large or incomplete" }));
  // Only same-origin pages may post: refuse cross-site form posts from other tabs.
  const origin = req.headers.origin;
  if (req.method === "POST" && origin && origin !== `http://${req.headers.host}`) {
    res.writeHead(403).end("cross-origin");
    return;
  }
  const json = (code, o) => res.writeHead(code, { "content-type": "application/json", "cache-control": "no-store" }).end(JSON.stringify(o));
  if (req.method === "GET" && req.url === "/") {
    res.writeHead(200, { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" }).end(page());
    return;
  }
  if (req.method === "GET" && req.url === "/api/gallery") return json(200, gallery());
  if (req.method === "GET" && req.url === "/api/comments") return json(200, commentList());
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
    return json(started ? 202 : 409, { started });
  }
  if (req.method === "POST" && req.url === "/comments") {
    const body = await readJson(req);
    if (!body) return tooBig();
    const t = String(body.text || "").trim().slice(0, 1000);
    const x = Math.round(Number(body.x)), y = Math.round(Number(body.y));
    if (!SHOT_RE.test(body.shot || "") || !SHA_RE.test(body.sha || "") || !t || !(x >= 0 && x <= 466 && y >= 0 && y <= 466))
      return json(400, { error: "bad note" });
    const n = nextC++;
    const c = { id: "c" + n, n, shot: body.shot, sha: body.sha, x, y, text: t, dev: String(body.dev || ""), created: Date.now(), sent: 0, seen: 0, answered: 0, batch: "", replies: [] };
    comments.set(c.id, c);
    if (!saveComments()) {
      comments.delete(c.id);
      nextC = n;
      broadcast("comments", commentList());
      return json(500, { error: "could not save note: " + saveError });
    }
    return json(200, c);
  }
  if (req.method === "POST" && req.url === "/comments/delete") {
    const body = await readJson(req);
    if (!body) return tooBig();
    const c = comments.get(String(body.id));
    if (!c || c.sent) return json(400, { error: "only drafts can be deleted" });
    comments.delete(c.id);
    if (!saveComments()) {
      comments.set(c.id, c);
      broadcast("comments", commentList());
      return json(500, { error: "could not delete note: " + saveError });
    }
    return json(200, { deleted: c.id });
  }
  if (req.method === "POST" && req.url === "/comments/send") {
    const body = await readJson(req);
    if (!body) return tooBig();
    const r = sendBatch(String(body.dev || ""));
    return json(r.pushed ? 500 : r.error ? 409 : 200, r);
  }
  if (req.method === "GET" && req.url === "/events") {
    res.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-store", connection: "keep-alive" });
    res.write(`event: hello\ndata: ${JSON.stringify(hello())}\n\n`);
    res.write(`event: session\ndata: ${JSON.stringify(session)}\n\n`);
    for (const p of [...presses.values()].slice(-30)) res.write(`event: press\ndata: ${JSON.stringify(p)}\n\n`);
    res.write(`event: comments\ndata: ${JSON.stringify(commentList())}\n\n`);
    clients.add(res);
    req.on("close", () => clients.delete(res));
    return;
  }
  if (req.method === "POST" && req.url === "/press") {
    const body = await readJson(req);
    if (!body) return tooBig();
    const kind = ["ping", "say", "agent"].includes(body.kind) ? body.kind : "ping";
    const t = String(body.text || "").slice(0, 2000);
    const sent = Number(body.sent) || Date.now(); // the page's clock
    const id = "b" + Date.now().toString(36);
    const p = { id, n: nextN++, kind, text: t, dev: String(body.dev || ""), sent, pushed: 0, answered: 0, replies: [] };
    if (session.connected) {
      push(kind === "say" ? t : `${kind} from the board (${p.dev || "browser"})`, { id, kind });
      p.pushed = Date.now();
    }
    record({ ev: "press", id, kind, sent, pushed: p.pushed, dev: p.dev });
    addPress(p);
    return json(200, { id, pushed: p.pushed, session: session.connected });
  }
  res.writeHead(404).end("not found");
};

const safeHandler = async (req, res) => {
  try { await handler(req, res); } catch (e) {
    log("handler error:", e && e.stack || e);
    if (!res.headersSent) res.writeHead(500, { "content-type": "application/json" }).end(JSON.stringify({ error: "internal error: " + (e && e.message) }));
    else res.end();
  }
};

// A restart can begin while the previous copy still holds the port: retry instead of giving up.
let listening = 0;
function listenOn(h) {
  const srv = http.createServer(safeHandler);
  servers.push(srv);
  srv.on("error", (e) => {
    log(`http error on ${h}:`, e.message);
    if (e.code === "EADDRINUSE") setTimeout(() => listenOn(h), 1000);
  });
  srv.listen(PORT, h, () => {
    log(`board on http://${h}:${PORT}`);
    if (++listening === HOSTS.length) toShim({ t: "ready" });
  });
}
for (const h of HOSTS) listenOn(h);
