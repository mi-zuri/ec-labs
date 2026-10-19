"use strict";

// ------------------------------------------------------------------ state

const $ = (id) => document.getElementById(id);
const LANG_ORDER = ["python", "c", "cpp", "rust"];
const COST_STOPS = [[42, 157, 143], [233, 196, 106], [228, 87, 46]]; // cheap -> expensive

const START_COLOR = "#e11d2e"; // the start node is marked with a red X, the first edges start red
const FADE_FRACTION = 0.15;  // the first 15% of the tour length fades from red to the tour colour

const S = {
  catalog: null,
  lab: null,        // lab object from the catalog
  instance: null,   // instance name
  inst: null,       // {nodes, k, bounds, costT, colors}
  method: null,     // method object
  lang: "c",
  mode: "trace",
  // playback
  frames: [],       // {tour, run?, obj?}
  cursor: -1,
  playing: false,
  speed: 20,
  acc: 0,
  lastTs: 0,
  dirty: true,
  hover: null,
  view: null,
  // current run
  session: null,    // {method, lang, mode, meta, runs, done, best}
  controller: null,
  batch: null,
  included: [],     // ids of the extra labs compared with the selected one
};

// ------------------------------------------------------------------ helpers

const fmtInt = (v) => Math.round(v).toLocaleString("en-US");
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const langName = (l) => (S.catalog.langs[l] || { name: l }).name;
const langColor = (l) => getComputedStyle(document.documentElement).getPropertyValue("--c-" + l).trim() || "#888";
const cssVar = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

function fmtTime(ns) {
  if (ns == null) return "–";
  if (ns < 1e3) return ns.toFixed(0) + " ns";
  if (ns < 1e6) return (ns / 1e3).toFixed(1) + " µs";
  if (ns < 1e9) return (ns / 1e6).toFixed(2) + " ms";
  return (ns / 1e9).toFixed(2) + " s";
}

function store(key, value) {
  try { localStorage.setItem("ec." + key, value); } catch (e) { /* storage unavailable */ }
}
function recall(key) {
  try { return localStorage.getItem("ec." + key); } catch (e) { return null; }
}

function setStatus(text, kind = "") {
  $("status-text").textContent = text;
  $("status").className = "status " + kind;
}

function showLog(text, isError) {
  const log = $("log");
  log.hidden = !text;
  log.textContent = text || "";
  log.classList.toggle("err", !!isError);
}

async function getJSON(url) {
  const r = await fetch(url);
  const body = await r.json();
  if (!r.ok) throw new Error(body.error || r.statusText);
  return body;
}

/** Run a solver through the server and call onMsg for every JSON line it streams. */
async function stream(params, onMsg, signal) {
  const r = await fetch("/api/run?" + new URLSearchParams(params), { signal });
  if (!r.ok) {
    const e = await r.json().catch(() => ({ error: r.statusText }));
    throw new Error(e.error);
  }
  const reader = r.body.getReader();
  const decoder = new TextDecoder();
  let buf = "";
  for (;;) {
    const { value, done } = await reader.read();
    if (done) break;
    buf += decoder.decode(value, { stream: true });
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
      const line = buf.slice(0, i).trim();
      buf = buf.slice(i + 1);
      if (line) onMsg(JSON.parse(line));
    }
  }
}

function stats(values) {
  let min = Infinity, max = -Infinity, sum = 0;
  for (const v of values) { min = Math.min(min, v); max = Math.max(max, v); sum += v; }
  return { min, max, avg: values.length ? sum / values.length : 0 };
}

function dist(a, b) {
  const p = S.inst.nodes[a], q = S.inst.nodes[b];
  return Math.floor(Math.sqrt((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2) + 0.5);
}

function evaluate(tour) {
  let len = 0, cost = 0;
  const m = tour.length;
  for (let i = 0; i < m; i++) {
    if (m > 1) len += dist(tour[i], tour[(i + 1) % m]);
    cost += S.inst.nodes[tour[i]][2];
  }
  return { len, cost, obj: len + cost };
}

/** Edges [a, b, fA, fB] at the start of the tour, a -> b.
 *  f is the fade position: 0 = red at the start node, 1 = tour colour after FADE_FRACTION of the tour length (fB may exceed 1). */
function* fadeEdges(tour) {
  const nodes = S.inst.nodes;
  const dist = (a, b) => Math.hypot(nodes[a][0] - nodes[b][0], nodes[a][1] - nodes[b][1]);
  let tourLength = 0;
  for (let i = 0; i < tour.length; i++) tourLength += dist(tour[i], tour[(i + 1) % tour.length]);
  const fadeLength = FADE_FRACTION * tourLength;
  let walked = 0; // distance from the start to the beginning of the current edge
  for (let i = 0; i + 1 < tour.length && walked < fadeLength; i++) {
    const length = dist(tour[i], tour[i + 1]);
    yield [tour[i], tour[i + 1], walked / fadeLength, (walked + length) / fadeLength];
    walked += length;
  }
}

function hexToRgb(hex) {
  const v = parseInt(hex.replace("#", ""), 16);
  return [v >> 16, (v >> 8) & 255, v & 255];
}

function costColor(t) {
  const seg = t < 0.5 ? 0 : 1, f = t < 0.5 ? t * 2 : (t - 0.5) * 2;
  const a = COST_STOPS[seg], b = COST_STOPS[seg + 1];
  return `rgb(${a.map((v, i) => Math.round(v + (b[i] - v) * f)).join(",")})`;
}

// ------------------------------------------------------------------ setup

async function init() {
  try {
    S.catalog = await getJSON("/api/catalog");
  } catch (e) {
    setStatus("Cannot reach the server. Start it with: python3 tool/server.py", "err");
    return;
  }
  if (!S.catalog.labs.length) return setStatus("No labs found (expected labs/labN/lab.json)", "err");

  for (const sel of [$("lab"), $("cmp-lab")])
    sel.innerHTML = S.catalog.labs.map((l) => `<option value="${l.id}">Lab ${l.num} · ${esc(l.title)}</option>`).join("");
  for (const sel of [$("instance"), $("cmp-instance")])
    sel.innerHTML = S.catalog.instances.map((n) => `<option>${esc(n)}</option>`).join("");

  S.lang = recall("lang") || "c";
  S.mode = recall("mode") || "trace";
  S.speed = Number(recall("speed") || 20);
  $("pb-speed").value = String(S.speed);

  const labId = recall("lab");
  selectLab(S.catalog.labs.find((l) => l.id === labId) ? labId : S.catalog.labs.at(-1).id, recall("method"));
  const instName = recall("instance");
  await selectInstance(S.catalog.instances.includes(instName) ? instName : S.catalog.instances[0]);
  setMode(S.mode);
  bindEvents();
  setStatus("Ready", "ok");
  requestAnimationFrame(tick);
}

function selectLab(id, methodId) {
  if (S.lab && S.lab.id !== id) S.included = []; // changing the lab starts again with that lab only
  S.lab = S.catalog.labs.find((l) => l.id === id);
  $("lab").value = $("cmp-lab").value = id;
  store("lab", id);
  $("method").innerHTML = S.lab.methods.map((m) => `<option value="${m.id}">${esc(m.name)}</option>`).join("");
  selectMethod(S.lab.methods.some((m) => m.id === methodId) ? methodId : S.lab.methods[0].id);
  renderLangButtons();
  if (!$("tab-compare").hidden) loadCompare();
}

// Labs shown in the Compare tab and the report: the selected lab (always) plus the extra labs ticked by the user.
function includedLabs() {
  return S.catalog.labs.filter((l) => l.id === S.lab.id || S.included.includes(l.id));
}

// Every solution to compare: {lab, method, name}. With several labs the name says where it comes from.
function compareEntries() {
  const labs = includedLabs();
  return labs.flatMap((lab) => lab.methods.map((method) => ({
    lab, method, name: labs.length > 1 ? `Lab ${lab.num}: ${method.name}` : method.name,
  })));
}

function renderIncludeChecks() {
  const on = includedLabs().map((l) => l.id);
  $("cmp-include").innerHTML = S.catalog.labs.map((l) =>
    `<label title="${esc(l.title)}"><input type="checkbox" value="${l.id}"${on.includes(l.id) ? " checked" : ""}${l.id === S.lab.id ? " disabled" : ""}> Lab ${l.num} · ${esc(l.title)}</label>`).join("");
}

function onIncludeChange() {
  S.included = [...$("cmp-include").querySelectorAll("input:checked:not(:disabled)")].map((c) => c.value);
  loadCompare();
}

function selectMethod(id) {
  S.method = S.lab.methods.find((m) => m.id === id);
  $("method").value = id;
  store("method", id);
}

async function selectInstance(name) {
  const data = await getJSON("/api/instance?name=" + encodeURIComponent(name));
  const xs = data.nodes.map((n) => n[0]), ys = data.nodes.map((n) => n[1]), cs = data.nodes.map((n) => n[2]);
  const cmin = Math.min(...cs), cmax = Math.max(...cs);
  const costT = cs.map((c) => (c - cmin) / (cmax - cmin || 1));
  S.inst = {
    nodes: data.nodes,
    k: data.k,
    bounds: { minX: Math.min(...xs), maxX: Math.max(...xs), minY: Math.min(...ys), maxY: Math.max(...ys) },
    costT,
    colors: costT.map(costColor),
  };
  S.instance = name;
  $("instance").value = $("cmp-instance").value = name;
  $("start").max = data.nodes.length - 1;
  store("instance", name);
  clearFrames();
  if (!$("tab-compare").hidden) loadCompare();
}

function availableLangs() {
  return LANG_ORDER.filter((l) => S.lab.langs.includes(l) && S.catalog.langs[l] && S.catalog.langs[l].available);
}

function renderLangButtons() {
  const ok = availableLangs();
  if (!ok.includes(S.lang) && ok.length) S.lang = ok[0];
  $("langs").innerHTML = LANG_ORDER.filter((l) => S.catalog.langs[l]).map((l) => {
    const info = S.catalog.langs[l];
    const reason = !S.lab.langs.includes(l) ? "No solver for this lab yet"
      : !info.available ? `${info.tool} not found on PATH — see README` : "";
    return `<button data-lang="${l}" class="${l === S.lang ? "active" : ""}" ${reason ? "disabled" : ""}
      title="${esc(reason || info.name)}">${esc(info.name)}</button>`;
  }).join("");
}

function setMode(mode) {
  S.mode = mode;
  store("mode", mode);
  for (const b of $("modes").children) b.classList.toggle("active", b.dataset.mode === mode);
  $("mode-hint").textContent = mode === "trace"
    ? "One run, every step streamed live. Its time includes printing the steps."
    : "All runs, one result line each. Time = algorithm only (fair comparison).";
  $("start-field").hidden = mode !== "trace";
}

// ------------------------------------------------------------------ running

function busy() { return !!(S.controller || S.batch); }

function updateButtons() {
  $("run").disabled = busy();
  $("stop").disabled = !S.controller;
  $("batch-missing").disabled = $("batch-all").disabled = busy();
  $("batch-stop").disabled = !S.batch;
}

function clearFrames() {
  S.frames = [];
  S.cursor = -1;
  S.acc = 0;
  S.session = null;
  S.dirty = true;
  $("runs-card").hidden = true;
  $("tour-card").hidden = true;
  $("stats").innerHTML = "";
  updatePlayback();
}

async function run() {
  if (busy()) return;
  clearFrames();
  showLog("");
  S.session = { method: S.method, lang: S.lang, mode: S.mode, meta: null, runs: [], done: null, best: null, error: null };
  S.playing = true;
  S.controller = new AbortController();
  updateButtons();
  setStatus(`Starting ${S.method.name} · ${langName(S.lang)}…`, "busy");
  const params = {
    lab: S.lab.id, lang: S.lang, method: S.method.id, instance: S.instance, mode: S.mode,
    start: Math.max(0, Number($("start").value) || 0),
  };
  try {
    await stream(params, onMessage, S.controller.signal);
    if (!S.session.done && !S.session.error) setStatus("Solver stopped without finishing", "err");
  } catch (e) {
    if (e.name === "AbortError") setStatus("Stopped", "");
    else setStatus(e.message, "err");
  } finally {
    S.controller = null;
    updateButtons();
    S.dirty = true;
    renderStats();
  }
}

function onMessage(msg) {
  const ses = S.session;
  switch (msg.t) {
    case "compile":
      if (msg.status === "building") setStatus("Compiling…", "busy");
      else if (msg.status === "error") { showLog((msg.cmd ? "$ " + msg.cmd + "\n" : "") + msg.log, true); setStatus("Compilation failed", "err"); ses.error = true; }
      else if (msg.status === "ok") { showLog(msg.log ? "$ " + msg.cmd + "\n" + msg.log : ""); setStatus(`Compiled in ${msg.ms} ms`, "busy"); }
      ses.compile = msg;
      break;
    case "queued":
      setStatus("Waiting for another benchmark to finish…", "busy");
      break;
    case "meta":
      ses.meta = msg;
      setStatus(`Running ${ses.method.name} · ${langName(ses.lang)} · ${msg.runs} run${msg.runs > 1 ? "s" : ""}`, "busy");
      break;
    case "step":
      S.frames.push({ tour: msg.tour });
      break;
    case "run_end":
      ses.runs.push(msg);
      if (!ses.best || msg.obj < ses.best.obj) ses.best = msg;
      if (ses.mode === "bench") S.frames.push({ tour: msg.tour, run: msg.run, obj: msg.obj });
      else if (!S.frames.length) S.frames.push({ tour: msg.tour });
      scheduleStats();
      break;
    case "done":
      ses.done = msg;
      setStatus(`Done · ${ses.runs.length} run${ses.runs.length > 1 ? "s" : ""} · wall ${fmtInt(msg.wall_ms)} ms` +
        (msg.saved ? " · saved" : ""), "ok");
      break;
    case "error":
      ses.error = true;
      showLog(msg.msg, true);
      setStatus("Solver error", "err");
      break;
    case "log":
      showLog(($("log").textContent ? $("log").textContent + "\n" : "") + msg.msg);
      break;
  }
  updatePlayback();
}

function stop() {
  if (S.controller) S.controller.abort();
  if (S.batch) S.batch.controller.abort();
}

let statsQueued = false;
function scheduleStats() {
  if (statsQueued) return;
  statsQueued = true;
  requestAnimationFrame(() => { statsQueued = false; renderStats(); });
}

function renderStats() {
  const ses = S.session;
  if (!ses || !ses.runs.length) { $("stats").innerHTML = ""; return; }
  const runs = ses.runs;
  const total = ses.meta ? ses.meta.runs : runs.length;
  const objs = stats(runs.map((r) => r.obj)), ns = stats(runs.map((r) => r.ns));
  const okCount = runs.filter((r) => r.ok).length;
  const checker = okCount === runs.length
    ? `<span class="ok">✓ ${okCount}/${runs.length} valid</span>`
    : `<span class="bad">✗ ${runs.length - okCount} invalid: ${esc(runs.find((r) => !r.ok).why)}</span>`;
  const head = `<h3>${esc(ses.method.name)} · ${esc(langName(ses.lang))}</h3>`;

  if (ses.mode === "trace") {
    const r = runs[0];
    $("stats").innerHTML = head + `<table>
      <tr><td>Run</td><td class="num">#${r.run}</td></tr>
      <tr><td>Objective</td><td class="num"><b>${fmtInt(r.obj)}</b></td></tr>
      <tr><td>Length + cost</td><td class="num">${fmtInt(r.len)} + ${fmtInt(r.cost)}</td></tr>
      <tr><td>Time (incl. output)</td><td class="num">${fmtTime(r.ns)}</td></tr>
      ${ses.done ? `<tr><td>Wall time</td><td class="num">${fmtInt(ses.done.wall_ms)} ms</td></tr>` : ""}
      <tr><td>Checker</td><td class="num">${checker}</td></tr></table>
      <p class="hint">Use Bench mode for fair timing.</p>`;
  } else {
    $("stats").innerHTML = head + `
      <div class="progress"><i style="width:${(100 * runs.length / total).toFixed(1)}%"></i></div>
      <table>
      <tr><td>Runs</td><td class="num">${runs.length} / ${total}</td></tr>
      <tr><td>Objective min</td><td class="num"><b>${fmtInt(objs.min)}</b></td></tr>
      <tr><td>Objective avg</td><td class="num">${fmtInt(objs.avg)}</td></tr>
      <tr><td>Objective max</td><td class="num">${fmtInt(objs.max)}</td></tr>
      <tr><td>Time / run (min)</td><td class="num">${fmtTime(ns.min)}</td></tr>
      <tr><td>Time / run (avg)</td><td class="num"><b>${fmtTime(ns.avg)}</b></td></tr>
      <tr><td>Time / run (max)</td><td class="num">${fmtTime(ns.max)}</td></tr>
      <tr><td>Algorithm total</td><td class="num">${fmtTime(runs.reduce((a, r) => a + r.ns, 0))}</td></tr>
      ${ses.done ? `<tr><td>Wall (process + I/O)</td><td class="num">${fmtInt(ses.done.wall_ms)} ms</td></tr>` : ""}
      <tr><td>Checker</td><td class="num">${checker}</td></tr></table>
      ${ses.done && ses.done.saved ? `<p class="hint">Saved to ${esc(ses.done.saved)}</p>` : ""}`;
  }

  const best = ses.best;
  $("tour-card").hidden = false;
  $("tour-title").textContent = ses.mode === "trace" ? `Solution (run #${best.run})` : `Best solution (run #${best.run}, objective ${fmtInt(best.obj)})`;
  $("tour-list").textContent = best.tour.join(", ");
  $("runs-card").hidden = ses.mode !== "bench";
  if (ses.mode === "bench") drawRunsChart();
}

// ------------------------------------------------------------------ playback

function tick(ts) {
  const dt = S.lastTs ? Math.min(ts - S.lastTs, 100) : 0;
  S.lastTs = ts;
  if (S.playing && S.frames.length) {
    const last = S.frames.length - 1;
    if (S.speed === Infinity) {
      if (S.cursor !== last) { S.cursor = last; S.dirty = true; }
    } else {
      S.acc += (dt * S.speed) / 1000;
      while (S.acc >= 1 && S.cursor < last) { S.cursor++; S.acc -= 1; S.dirty = true; }
      if (S.cursor >= last) S.acc = Math.min(S.acc, 1);
    }
    if (S.cursor >= last && !S.controller) { S.playing = false; updatePlayback(); }
  }
  if (S.dirty) {
    S.dirty = false;
    render();
    updatePlayback();
  }
  requestAnimationFrame(tick);
}

function seek(i) {
  S.playing = false;
  S.cursor = Math.max(0, Math.min(S.frames.length - 1, i));
  S.dirty = true;
}

function togglePlay() {
  if (!S.frames.length) return;
  if (!S.playing && S.cursor >= S.frames.length - 1 && !S.controller) S.cursor = -1; // replay from start
  S.playing = !S.playing;
  S.acc = 0;
  updatePlayback();
}

function updatePlayback() {
  const n = S.frames.length;
  const range = $("pb-range");
  range.max = Math.max(0, n - 1);
  range.value = Math.max(0, S.cursor);
  const unit = S.session && S.session.mode === "bench" ? "run" : "step";
  $("pb-counter").textContent = n ? `${unit} ${Math.max(0, S.cursor) + 1} / ${n}` : "0 / 0";
  $("pb-play").textContent = S.playing ? "⏸" : "▶";
  for (const id of ["pb-first", "pb-prev", "pb-next", "pb-last", "pb-play"]) $(id).disabled = !n;
}

// ------------------------------------------------------------------ drawing

function sizeCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth, h = canvas.clientHeight;
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  return { ctx, w, h };
}

/** Draw nodes (colored and sized by cost) and a tour. Returns the coordinate mapping. */
function drawPlot(canvas, tour, o = {}) {
  const { ctx, w, h } = sizeCanvas(canvas);
  const nodes = S.inst.nodes, b = S.inst.bounds, k = S.inst.k;
  const pad = o.small ? 10 : 30;
  const s = Math.min((w - 2 * pad) / (b.maxX - b.minX || 1), (h - 2 * pad) / (b.maxY - b.minY || 1));
  const ox = (w - s * (b.maxX - b.minX)) / 2, oy = (h - s * (b.maxY - b.minY)) / 2;
  const X = (i) => ox + s * (nodes[i][0] - b.minX);
  const Y = (i) => h - oy - s * (nodes[i][1] - b.minY);
  const tourColor = cssVar("--tour"), fg = cssVar("--fg");

  if (tour && tour.length > 1) {
    const closed = tour.length >= k || o.shape === "cycle";
    ctx.strokeStyle = tourColor;
    ctx.lineWidth = o.small ? 1.1 : 1.8;
    ctx.lineJoin = "round";
    ctx.beginPath();
    ctx.moveTo(X(tour[0]), Y(tour[0]));
    for (let i = 1; i < tour.length; i++) ctx.lineTo(X(tour[i]), Y(tour[i]));
    if (closed) ctx.closePath();
    ctx.stroke();
    if (!closed && tour.length > 2) { // a path still under construction: show the edge that will close it
      ctx.save();
      ctx.setLineDash([4, 5]);
      ctx.globalAlpha = 0.4;
      ctx.beginPath();
      ctx.moveTo(X(tour.at(-1)), Y(tour.at(-1)));
      ctx.lineTo(X(tour[0]), Y(tour[0]));
      ctx.stroke();
      ctx.restore();
    }
  }

  if (tour && tour.length > 1) { // the start of the tour fades from red to the tour colour: shows where it starts and its direction
    const from = hexToRgb(START_COLOR), to = hexToRgb(tourColor);
    const fadeColor = (f) => `rgb(${from.map((v, c) => Math.round(v + (to[c] - v) * Math.min(f, 1))).join(",")})`;
    ctx.lineWidth = o.small ? 1.8 : 3;
    ctx.lineCap = "round";
    for (const [a, b, fA, fB] of fadeEdges(tour)) {
      const gradient = ctx.createLinearGradient(X(a), Y(a), X(b), Y(b));
      gradient.addColorStop(0, fadeColor(fA));
      gradient.addColorStop(Math.min(1, (1 - fA) / (fB - fA)), fadeColor(fB)); // the fade is complete here, the rest of the edge keeps this colour
      ctx.strokeStyle = gradient;
      ctx.beginPath();
      ctx.moveTo(X(a), Y(a));
      ctx.lineTo(X(b), Y(b));
      ctx.stroke();
    }
    ctx.lineCap = "butt";
  }

  const inTour = new Set(tour || []);
  const anyTour = inTour.size > 0;
  for (let i = 0; i < nodes.length; i++) {
    const t = S.inst.costT[i];
    const r = o.small ? 1.2 + t * 2.6 : 2.6 + t * 5.4;
    ctx.globalAlpha = !anyTour || inTour.has(i) ? 1 : 0.28;
    ctx.fillStyle = S.inst.colors[i];
    ctx.beginPath();
    ctx.arc(X(i), Y(i), r, 0, Math.PI * 2);
    ctx.fill();
    if (inTour.has(i) && !o.small) {
      ctx.globalAlpha = 0.6;
      ctx.lineWidth = 1;
      ctx.strokeStyle = fg;
      ctx.stroke();
    }
  }
  ctx.globalAlpha = 1;

  if (tour && tour.length) { // the start node (first in the tour) is marked with a red X
    const x = X(tour[0]), y = Y(tour[0]), d = o.small ? 3.5 : 7;
    ctx.lineCap = "round";
    for (const [color, width] of [["#fff", o.small ? 3.4 : 5], [START_COLOR, o.small ? 1.6 : 2.6]]) {
      ctx.strokeStyle = color;
      ctx.lineWidth = width;
      ctx.beginPath();
      ctx.moveTo(x - d, y - d); ctx.lineTo(x + d, y + d);
      ctx.moveTo(x - d, y + d); ctx.lineTo(x + d, y - d);
      ctx.stroke();
    }
    ctx.lineCap = "butt";
  }

  const ring = (i, color, radius) => {
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(X(i), Y(i), radius, 0, Math.PI * 2);
    ctx.stroke();
  };
  if (o.highlight != null) ring(o.highlight, tourColor, 11);
  if (o.hover != null) ring(o.hover, fg, 9);
  return { X, Y };
}

function render() {
  if (!S.inst) return;
  const frame = S.frames[S.cursor];
  const tour = frame ? frame.tour : null;
  let highlight = null;
  if (frame && !("run" in frame) && S.cursor > 0) {
    const prev = new Set(S.frames[S.cursor - 1].tour);
    highlight = tour.find((v) => !prev.has(v));
  }
  S.view = drawPlot($("plot"), tour, { shape: S.session ? S.session.method.shape : "cycle", highlight, hover: S.hover });
  renderOverlay(frame);
  if (S.session && S.session.mode === "bench" && !$("runs-card").hidden) drawRunsChart();
}

function renderOverlay(frame) {
  const ses = S.session;
  if (!frame) {
    $("overlay").innerHTML = `<b>${esc(S.instance)}</b> · ${S.inst.nodes.length} nodes · select ${S.inst.k}` +
      `<br><span class="hint">Choose a method and language, then Run.</span>`;
    return;
  }
  const e = evaluate(frame.tour);
  const title = ses ? `<b>${esc(ses.method.name)}</b> · ${esc(langName(ses.lang))}` : `<b>${esc(frame.label || "")}</b>`;
  const where = frame.run != null ? `run #${frame.run}` : `nodes ${frame.tour.length} / ${S.inst.k}`;
  const partial = frame.tour.length < S.inst.k ? " (partial, as cycle)" : "";
  $("overlay").innerHTML = `${title} · ${where}<br><span class="big">${fmtInt(e.obj)}</span>` +
    ` <span class="hint">= ${fmtInt(e.len)} length + ${fmtInt(e.cost)} cost${partial}</span>`;
}

function drawRunsChart() {
  const canvas = $("runs-chart");
  const { ctx, w, h } = sizeCanvas(canvas);
  const runs = S.session.runs;
  if (!runs.length) return;
  const total = S.session.meta ? S.session.meta.runs : runs.length;
  const st = stats(runs.map((r) => r.obj));
  const pad = { l: 56, r: 10, t: 8, b: 18 };
  const x = (i) => pad.l + (i + 0.5) * (w - pad.l - pad.r) / total;
  const y = (v) => pad.t + (h - pad.t - pad.b) * (st.max === st.min ? 0.5 : (st.max - v) / (st.max - st.min));
  const muted = cssVar("--muted"), line = cssVar("--line"), accent = cssVar("--accent"), good = cssVar("--good");

  ctx.font = "11px system-ui";
  ctx.fillStyle = muted;
  ctx.strokeStyle = line;
  ctx.lineWidth = 1;
  for (const v of [st.min, (st.min + st.max) / 2, st.max]) {
    ctx.beginPath();
    ctx.moveTo(pad.l, y(v));
    ctx.lineTo(w - pad.r, y(v));
    ctx.stroke();
    ctx.textAlign = "right";
    ctx.fillText(fmtInt(v), pad.l - 6, y(v) + 4);
  }
  ctx.textAlign = "left";
  ctx.fillText("run 0", pad.l, h - 4);
  ctx.textAlign = "right";
  ctx.fillText(`run ${total - 1}`, w - pad.r, h - 4);

  const current = S.frames[S.cursor];
  runs.forEach((r, i) => {
    const isBest = r === S.session.best, isCur = current && current.run === r.run;
    ctx.fillStyle = isBest ? good : accent;
    ctx.globalAlpha = isBest || isCur ? 1 : 0.55;
    ctx.beginPath();
    ctx.arc(x(i), y(r.obj), isBest || isCur ? 4 : 2.2, 0, Math.PI * 2);
    ctx.fill();
  });
  ctx.globalAlpha = 1;
  S.runsChart = { x0: pad.l, x1: w - pad.r, total };
}

// ------------------------------------------------------------------ compare tab

async function loadCompare() {
  loadReport();
  renderIncludeChecks();
  const entries = compareEntries();
  const results = await getJSON(`/api/results?lab=${includedLabs().map((l) => l.id).join(",")}&instance=${encodeURIComponent(S.instance)}`);
  const by = {}; // by[lab id + "/" + method id][lang]
  for (const r of results) (by[r.lab + "/" + r.method] = by[r.lab + "/" + r.method] || {})[r.lang] = r;
  S.compare = by;
  const langs = LANG_ORDER.filter((l) => entries.some((e) => e.lab.langs.includes(l)));

  // objective table: languages usually agree, so each score is shown once
  let html = `<tr><th>Method</th><th>Average (min – max)</th><th>Same results?</th></tr>`;
  const scoreOf = (key) => {
    const v = Object.values(by[key] || {}).map((r) => r.stats.obj_avg);
    return v.length ? v.reduce((a, b) => a + b, 0) / v.length : null;
  };
  const scores = entries.map((e) => scoreOf(e.lab.id + "/" + e.method.id)).filter((v) => v != null);
  const maxScore = Math.max(0, ...scores); // one scale for all compared solutions
  // Random is far worse than everything else, so it stays gray and out of the colour range.
  const graded = entries.filter((e) => e.method.id !== "random")
    .map((e) => scoreOf(e.lab.id + "/" + e.method.id)).filter((v) => v != null);
  const bestGraded = Math.min(...graded), worstGraded = Math.max(...graded);
  for (const { lab, method: m, name } of entries) {
    const row = by[lab.id + "/" + m.id] || {};
    const sc = scoreOf(lab.id + "/" + m.id);
    const scoreBar = sc == null ? "" : `<div class="bar-row score-row">
        <div class="bar" style="width:${(100 * sc / maxScore).toFixed(2)}%;background:${m.id === "random" ? "var(--muted)" : scoreColor(sc, bestGraded, worstGraded)}"></div></div>`;
    html += `<tr><td><b>${esc(name)}</b>${scoreBar}</td><td>${scoreCell(row)}</td><td>${matchCell(row)}</td></tr>`;
  }
  $("cmp-table").innerHTML = html;

  // time bars
  let bars = "";
  const allNs = results.filter((r) => langs.includes(r.lang)).map((r) => r.stats.ns_avg);
  const maxNs = Math.max(...allNs); // one scale for all compared solutions
  for (const { lab, method: m, name } of entries) {
    const row = by[lab.id + "/" + m.id] || {};
    const have = langs.filter((l) => row[l]);
    if (!have.length) continue;
    bars += `<div class="mname">${esc(name)}</div>`;
    for (const l of have) {
      const ns = row[l].stats.ns_avg;
      bars += `<div class="lname">${esc(langName(l))}</div><div class="bar-row">
        <div class="bar" style="width:${(100 * ns / maxNs).toFixed(2)}%;background:${langColor(l)}"></div>
        <span class="bar-label">${fmtTime(ns)}</span></div>`;
    }
  }
  $("time-bars").innerHTML = bars ? `<div class="bars">${bars}</div>` : `<p class="hint">No benchmarks yet. Click “Run missing benchmarks”.</p>`;

  // best solutions
  const grid = $("best-grid");
  grid.innerHTML = "";
  for (const { lab, method: m, name } of entries) {
    const all = Object.values(by[lab.id + "/" + m.id] || {});
    if (!all.length) continue;
    const best = all.reduce((a, b) => (b.best.obj < a.best.obj ? b : a));
    const fastest = all.reduce((a, b) => (b.stats.ns_avg < a.stats.ns_avg ? b : a));
    const card = document.createElement("div");
    card.className = "panel best-card";
    card.innerHTML = `<div class="card-head">${esc(name)}</div><canvas></canvas>
      <div class="meta"><b>${fmtInt(best.best.obj)}</b> <span class="hint">= ${fmtInt(best.best.len)} + ${fmtInt(best.best.cost)} · run #${best.best.run} · ${esc(langName(fastest.lang))}</span></div>`;
    card.addEventListener("click", () => openBest(lab, m, best));
    grid.appendChild(card);
    requestAnimationFrame(() => drawPlot(card.querySelector("canvas"), best.best.tour, { small: true }));
  }
}

// Lower objective is better: the best compared method is green, the worst red.
function scoreColor(score, best, worst) {
  const t = worst > best ? (score - best) / (worst - best) : 0;
  return `hsl(${Math.round(120 * (1 - t))}, 65%, 48%)`;
}

// Scores of the first language that ran; matchCell reports any language that differs.
function scoreCell(row) {
  const first = LANG_ORDER.find((l) => row[l]);
  if (!first) return `<span class="hint">not run yet</span>`;
  const st = row[first].stats;
  return `<div class="num">${fmtInt(st.obj_avg)}</div>
    <div class="sub">${fmtInt(st.obj_min)} – ${fmtInt(st.obj_max)}</div>`;
}

function matchCell(row) {
  const langs = LANG_ORDER.filter((l) => row[l]);
  const failed = langs.filter((l) => !row[l].checked);
  const checker = failed.length ? `<div class="bad">✗ checker failed: ${failed.map(langName).join(", ")}</div>` : "";
  if (langs.length < 2) return `<span class="hint">needs 2+ languages</span>${checker}`;
  const base = row[langs[0]].objs;
  const diffs = langs.slice(1).filter((l) => row[l].objs.length !== base.length || row[l].objs.some((v, i) => v !== base[i]));
  if (!diffs.length) return `<span class="ok" title="identical in ${langs.length} languages">✓</span>${checker}`;
  return `<span class="bad">✗ ${diffs.map(langName).join(", ")} differ from ${langName(langs[0])}</span>${checker}`;
}

function openBest(lab, method, result) {
  if (busy()) return;
  switchTab("run");
  selectLab(lab.id, method.id);
  // Runs are deterministic (start = run index, fixed seed), so replay the best run in trace mode.
  S.lang = result.lang;
  renderLangButtons();
  setMode("trace");
  $("start").value = result.best.run;
  run();
}

async function runBatch(onlyMissing) {
  if (busy()) return;
  await loadCompare();
  const jobs = [];
  for (const { lab, method: m, name } of compareEntries())
    for (const l of LANG_ORDER.filter((l) => lab.langs.includes(l) && S.catalog.langs[l] && S.catalog.langs[l].available))
      if (!onlyMissing || !(S.compare[lab.id + "/" + m.id] && S.compare[lab.id + "/" + m.id][l])) jobs.push({ lab, m, l, name });
  if (!jobs.length) { $("batch-progress").textContent = "Everything is already benchmarked. Use “Re-run all” to refresh."; return; }

  S.batch = { controller: new AbortController() };
  updateButtons();
  setStatus("Running benchmarks…", "busy");
  const failed = [];
  try {
    for (let j = 0; j < jobs.length; j++) {
      const { lab, m, l, name } = jobs[j];
      const label = `${j + 1} / ${jobs.length} · ${name} · ${langName(l)}`;
      $("batch-progress").textContent = label + " · starting…";
      let done = 0, total = 0, error = null;
      await stream({ lab: lab.id, lang: l, method: m.id, instance: S.instance, mode: "bench" }, (msg) => {
        if (msg.t === "compile" && msg.status === "building") $("batch-progress").textContent = label + " · compiling…";
        if (msg.t === "compile" && msg.status === "error") error = "compile error";
        if (msg.t === "queued") $("batch-progress").textContent = label + " · waiting for another benchmark…";
        if (msg.t === "meta") total = msg.runs;
        if (msg.t === "error") error = msg.msg;
        if (msg.t === "run_end") { done++; $("batch-progress").textContent = `${label} · run ${done} / ${total}`; }
      }, S.batch.controller.signal);
      if (error) failed.push(`${name} · ${langName(l)}: ${error.split("\n")[0]}`);
      await loadCompare();
    }
    $("batch-progress").textContent = failed.length ? "Finished with errors: " + failed.join("; ") : `Finished ${jobs.length} benchmark${jobs.length > 1 ? "s" : ""}.`;
    setStatus("Benchmarks finished", failed.length ? "err" : "ok");
  } catch (e) {
    $("batch-progress").textContent = e.name === "AbortError" ? "Stopped." : "Error: " + e.message;
    setStatus(e.name === "AbortError" ? "Stopped" : e.message, e.name === "AbortError" ? "" : "err");
  } finally {
    S.batch = null;
    updateButtons();
  }
}

async function loadReport() {
  const id = S.lab.id;
  const r = await getJSON("/api/report?lab=" + id);
  if (S.lab.id !== id) return;
  $("report-gen").textContent = r.md ? "Regenerate report" : "Generate report";
  $("report-gen").title = r.md ? "Rebuild the PDF from the Markdown, refreshing results and visualisations" : "Create Report.md and the PDF report for this lab";
  $("report-links").innerHTML = (r.pdf ? `<a class="chip-link" href="/report/${id}/${encodeURIComponent(r.pdf_name)}" target="_blank">Open PDF</a>` : "")
    + (r.md ? `<button class="chip-link" id="report-edit">Edit</button>` : "");
  if (r.md) $("report-edit").addEventListener("click", editReport);
  $("report-file").textContent = r.md ? `File: labs/${id}/report/Report.md` : "";
}

async function makeReport() {
  const btn = $("report-gen"), msg = $("report-msg");
  btn.disabled = true;
  msg.textContent = "Generating…";
  try {
    const res = await fetch(`/api/report?lab=${S.lab.id}&include=${includedLabs().map((l) => l.id).join(",")}`, { method: "POST" });
    const r = await res.json();
    if (r.error) throw new Error(r.error);
    msg.textContent = (r.created ? "Report created. " : "Report regenerated. ") + (r.warnings.length ? "Warnings: " + r.warnings.join("; ") : "");
  } catch (e) {
    msg.textContent = "Failed: " + e.message;
  }
  btn.disabled = false;
  loadReport();
}

function copyText(text, button) {
  const original = button.textContent;
  navigator.clipboard.writeText(text).then(
    () => { button.textContent = "Copied"; setTimeout(() => (button.textContent = original), 1200); },
    () => { button.textContent = "Copy failed"; setTimeout(() => (button.textContent = original), 1200); },
  );
}

// ------------------------------------------------------------------ events

function switchTab(name) {
  for (const b of document.querySelectorAll(".tab-btn")) b.classList.toggle("active", b.dataset.tab === name);
  $("tab-run").hidden = name !== "run";
  $("tab-compare").hidden = name !== "compare";
  if (name === "compare") loadCompare();
  else S.dirty = true;
}

function nodeAt(ev) {
  if (!S.view) return null;
  const rect = $("plot").getBoundingClientRect();
  const mx = ev.clientX - rect.left, my = ev.clientY - rect.top;
  let best = null, bestD = 12 * 12;
  for (let i = 0; i < S.inst.nodes.length; i++) {
    const d = (S.view.X(i) - mx) ** 2 + (S.view.Y(i) - my) ** 2;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

function bindEvents() {
  for (const b of document.querySelectorAll(".tab-btn")) b.addEventListener("click", () => switchTab(b.dataset.tab));
  $("lab").addEventListener("change", (e) => selectLab(e.target.value));
  $("cmp-lab").addEventListener("change", (e) => selectLab(e.target.value));
  $("cmp-include").addEventListener("change", onIncludeChange);
  $("instance").addEventListener("change", (e) => selectInstance(e.target.value));
  $("cmp-instance").addEventListener("change", (e) => selectInstance(e.target.value));
  $("method").addEventListener("change", (e) => selectMethod(e.target.value));
  $("langs").addEventListener("click", (e) => {
    const b = e.target.closest("button[data-lang]");
    if (!b || b.disabled) return;
    S.lang = b.dataset.lang;
    store("lang", S.lang);
    renderLangButtons();
  });
  $("modes").addEventListener("click", (e) => {
    const b = e.target.closest("button[data-mode]");
    if (b) setMode(b.dataset.mode);
  });
  $("rand-start").addEventListener("click", () => { $("start").value = Math.floor(Math.random() * S.inst.nodes.length); });
  $("run").addEventListener("click", run);
  $("stop").addEventListener("click", stop);
  $("batch-stop").addEventListener("click", stop);
  $("batch-missing").addEventListener("click", () => runBatch(true));
  $("batch-all").addEventListener("click", () => runBatch(false));
  $("report-gen").addEventListener("click", makeReport);
  $("report-save").addEventListener("click", saveReport);
  $("report-close").addEventListener("click", () => { $("report-editor").hidden = true; });
  $("copy-tour").addEventListener("click", () => copyText($("tour-list").textContent, $("copy-tour")));

  $("pb-first").addEventListener("click", () => seek(0));
  $("pb-prev").addEventListener("click", () => seek(S.cursor - 1));
  $("pb-next").addEventListener("click", () => seek(S.cursor + 1));
  $("pb-last").addEventListener("click", () => seek(S.frames.length - 1));
  $("pb-play").addEventListener("click", togglePlay);
  $("pb-range").addEventListener("input", (e) => seek(Number(e.target.value)));
  $("pb-speed").addEventListener("change", (e) => { S.speed = Number(e.target.value); store("speed", e.target.value); });

  document.addEventListener("keydown", (e) => {
    if ($("tab-run").hidden || e.target.matches("input, select, textarea")) return;
    if (e.key === " ") { e.preventDefault(); togglePlay(); }
    else if (e.key === "ArrowLeft") seek(S.cursor - 1);
    else if (e.key === "ArrowRight") seek(S.cursor + 1);
    else if (e.key === "Home") seek(0);
    else if (e.key === "End") seek(S.frames.length - 1);
    else if (e.key === "Enter" && !busy()) run();
  });

  const plot = $("plot");
  plot.addEventListener("mousemove", (e) => {
    const i = nodeAt(e);
    if (i !== S.hover) { S.hover = i; S.dirty = true; }
    const tip = $("tip");
    if (i == null) { tip.hidden = true; return; }
    const [x, y, c] = S.inst.nodes[i];
    const frame = S.frames[S.cursor];
    const pos = frame ? frame.tour.indexOf(i) : -1;
    tip.innerHTML = `node <b>${i}</b> · cost ${c} · (${x}, ${y})${pos >= 0 ? ` · position ${pos}` : ""}`;
    tip.hidden = false;
    tip.style.left = Math.min(S.view.X(i) + 12, plot.clientWidth - tip.offsetWidth - 4) + "px";
    tip.style.top = S.view.Y(i) + 12 + "px";
  });
  plot.addEventListener("mouseleave", () => { S.hover = null; $("tip").hidden = true; S.dirty = true; });
  plot.addEventListener("click", (e) => {
    const i = nodeAt(e);
    if (i == null) return;
    $("start").value = i;
    if (S.mode !== "trace") setMode("trace");
  });

  $("runs-chart").addEventListener("click", (e) => {
    if (!S.runsChart || !S.session) return;
    const rect = $("runs-chart").getBoundingClientRect();
    const { x0, x1, total } = S.runsChart;
    const idx = Math.floor(((e.clientX - rect.left - x0) / (x1 - x0)) * total);
    const f = S.frames.findIndex((fr) => fr.run === idx);
    if (f >= 0) seek(f);
  });

  new ResizeObserver(() => { S.dirty = true; }).observe($("plot-wrap"));
  window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => {
    S.dirty = true;
    if (!$("tab-compare").hidden) loadCompare();
  });
}

init();

async function editReport() {
  const r = await getJSON("/api/report/md?lab=" + S.lab.id);
  $("report-text").value = r.text;
  $("report-editor").hidden = false;
  $("report-text").focus();
}

async function saveReport() {
  const text = $("report-text").value;
  $("report-editor").hidden = true;
  $("report-msg").textContent = "Saving…";
  try {
    const res = await fetch("/api/report/md?lab=" + S.lab.id, { method: "POST", body: text });
    const r = await res.json();
    if (r.error) throw new Error(r.error);
  } catch (e) {
    $("report-msg").textContent = "Failed: " + e.message;
    return;
  }
  makeReport();
}
