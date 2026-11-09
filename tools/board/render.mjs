#!/usr/bin/env node
// Board gallery renderer: run every simulator script that takes screenshots (`shot FILE.png`) and
// collect the PNGs under build/board/<ui-sha>/. <ui-sha> is the last commit touching firmware/ or
// simulator/ (+ "-dirty" when those trees have uncommitted changes), so a doc-only commit does not
// make the gallery stale. Progress is printed as JSON lines on stdout (board.mjs relays them).
//
//   node tools/board/render.mjs            render (builds the simulator first)
//   node tools/board/render.mjs --sha      print the current ui sha and exit

import { execFileSync, spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const SIM_DIR = path.join(ROOT, "simulator");
const SIM = path.join(SIM_DIR, "build", "agentlink_sim");
const OUT_ROOT = path.join(ROOT, "build", "board");
const UI_PATHS = ["firmware/components", "firmware/boards", "firmware/main", "simulator"];

const say = (o) => process.stdout.write(JSON.stringify(o) + "\n");
const git = (...a) => execFileSync("git", a, { cwd: ROOT, encoding: "utf8" }).trim();

export function uiSha() {
  const sha = git("log", "-1", "--format=%h", "--", ...UI_PATHS);
  const dirty = git("status", "--porcelain", "--", ...UI_PATHS).length > 0;
  return dirty ? `${sha}-dirty` : sha;
}

if (process.argv.includes("--sha")) {
  console.log(uiSha());
  process.exit(0);
}

const sha = uiSha();
const out = path.join(OUT_ROOT, sha);
fs.mkdirSync(out, { recursive: true });
say({ ev: "start", sha });

try {
  execFileSync("cmake", ["--build", "build", "-j"], { cwd: SIM_DIR, stdio: "ignore" });
} catch {
  try {
    execFileSync("cmake", ["-S", ".", "-B", "build"], { cwd: SIM_DIR, stdio: "ignore" });
    execFileSync("cmake", ["--build", "build", "-j"], { cwd: SIM_DIR, stdio: "ignore" });
  } catch (e) {
    say({ ev: "error", error: "simulator build failed" });
    process.exit(1);
  }
}

const scriptsDir = path.join(SIM_DIR, "scripts");
const scripts = fs.readdirSync(scriptsDir).filter((f) => f.endsWith(".txt")).sort()
  .filter((f) => /^shot\s+\S+/m.test(fs.readFileSync(path.join(scriptsDir, f), "utf8")));

function run(script) {
  return new Promise((resolve) => {
    const t0 = Date.now();
    const p = spawn(SIM, ["--headless", "--no-ws", "--script", path.join(scriptsDir, script)], { cwd: out });
    let log = "";
    p.stdout.on("data", (d) => { log += d; });
    p.stderr.on("data", (d) => { log += d; });
    const timer = setTimeout(() => p.kill("SIGKILL"), 90000);
    p.on("close", (code) => {
      clearTimeout(timer);
      const shots = [...log.matchAll(/"ev":"screenshot","file":"([^"]+)"/g)].map((m) => path.basename(m[1], ".png"));
      const fail = (log.match(/SCRIPT FAIL[^\n]*/) || [])[0] || (code ? `exit ${code}` : "");
      const r = { script, ok: !fail, ms: Date.now() - t0, shots, error: fail || undefined };
      say({ ev: "script", ...r });
      resolve(r);
    });
  });
}

// A few at a time: some scripts wait tens of seconds of simulated time.
const results = [];
const queue = [...scripts];
await Promise.all(Array.from({ length: 6 }, async () => {
  while (queue.length) results.push(await run(queue.shift()));
}));
results.sort((a, b) => a.script.localeCompare(b.script));

const index = { sha, at: Date.now(), source: "sim", scripts: results, shots: results.flatMap((r) => r.shots) };
fs.writeFileSync(path.join(out, "index.json"), JSON.stringify(index, null, 1));
fs.writeFileSync(path.join(OUT_ROOT, "latest.json"), JSON.stringify({ sha, at: index.at }));
say({ ev: "done", sha, shots: index.shots.length, failed: results.filter((r) => !r.ok).length });
