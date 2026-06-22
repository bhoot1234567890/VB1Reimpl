// =============================================================================
// app.js — main thread: UI + Web Audio wiring for the VB-1 web frontend.
// Imports the real excitation tables (parsed from the binary dump) and sends
// them to the AudioWorklet. All DSP runs in vb1-worklet.js on the audio thread.
// =============================================================================
import { EXC_TABLES, PROGRAM_TABLE } from "./excitation-tables.js";

// 16 factory programs. Columns: Damper, PickUp, Pick, Release, Shape, Volume.
const PRESETS = [
  ["Bassic Bass",  [0.1,        0.75,       0.33329999, 0.98, 0.0,       0.8]],
  ["Sustain Bass", [0.60227299, 0.91304302, 0.53953499, 0.98, 0.0,       0.8]],
  ["Round Bass",   [0.35227299, 0.95652199, 1.0,        0.98, 0.0,       0.8]],
  ["Fretless",     [1.0,        0.0,        1.0,        0.98, 0.0,       0.8]],
  ["Synthi Bass",  [0.113636,   0.289855,   0.0,        0.98, 0.0,       0.8]],
  ["Clavinet",     [0.625,      0.30434799, 0.0,        0.98, 1.0,       0.8]],
  ["DX Bass",      [0.63636398, 0.52173901, 0.40465099, 0.98, 0.79828799, 0.8]],
  ["Hollow Bass",  [0.715909,   0.39130399, 0.34883699, 0.98, 0.24428099, 0.8]],
  ["Sequenz Bass", [0.136364,   0.0,        1.0,        0.98, 0.228516,   0.8]],
  ["Warm Bass",    [0.80681801, 0.0,        0.40465099, 0.98, 0.160605,   0.8]],
  ["Slap Frets",   [0.238636,   0.0,        0.0,        0.98, 1.0,       0.8]],
  ["Buzz Bass",    [1.0,        0.55072498, 0.20930199, 0.98, 0.419047,   0.8]],
  ["Add Chorus",   [0.67045498, 0.0,        0.45116299, 0.98, 0.83333302, 0.8]],
  ["Synth Bass 2", [1.0,        0.30434799, 1.0,        0.98, 0.387485,   0.8]],
  ["Dark Click",   [0.88636398, 0.18840601, 0.38604599, 0.98, 0.118538,   0.8]],
  ["Synth Bass 3", [0.147727,   1.0,        0.0,        0.98, 0.67822999, 0.8]],
];
const PARAM_NAMES = ["damper", "pickup", "pick", "release", "shape", "volume"];

// ---- element refs ----------------------------------------------------------
const $ = (id) => document.getElementById(id);
const startBtn = $("startBtn");
const startOverlay = $("startOverlay");
const studio = $("studio");
const statusEl = $("status");
const srEl = $("sr");
const programSel = $("program");
const piano = $("piano");
const scope = $("scope");

// ---- audio state -----------------------------------------------------------
let ctx = null;
let node = null;
let analyser = null;
let currentProgram = 0;

const post = (msg) => { if (node) node.port.postMessage(msg); };

// ---- param sliders ---------------------------------------------------------
const sliders = {};
PARAM_NAMES.forEach((name) => {
  const el = $(name);
  const val = $(name + "Val");
  sliders[name] = { el, val };
  el.addEventListener("input", () => {
    const v = parseFloat(el.value);
    val.textContent = v.toFixed(3);
    post({ type: "param", name, value: v });
  });
});

function setSliderValues(paramsArr) {
  PARAM_NAMES.forEach((name, i) => {
    const v = paramsArr[i];
    sliders[name].el.value = v;
    sliders[name].val.textContent = v.toFixed(3);
  });
}

// ---- program selector ------------------------------------------------------
PRESETS.forEach(([name], i) => {
  const opt = document.createElement("option");
  opt.value = i; opt.textContent = `${i}: ${name}`;
  programSel.appendChild(opt);
});
programSel.addEventListener("change", () => {
  const i = parseInt(programSel.value, 10);
  currentProgram = i;
  const [, vals] = PRESETS[i];
  setSliderValues(vals);
  post({ type: "program", program: i, params: paramObj(vals) });
});

function paramObj(vals) {
  const o = {};
  PARAM_NAMES.forEach((n, i) => (o[n] = vals[i]));
  return o;
}

// ---- piano keyboard --------------------------------------------------------
const WHITE = [0, 2, 4, 5, 7, 9, 11];        // semitone offsets of white keys
const BLACK = { 1: 0, 3: 1, 6: 3, 8: 4, 10: 5 }; // semitone -> white-index-to-its-left
let baseNote = 36;                            // C2 (bass)
const held = new Set();

const KEYMAP = { a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11, k: 12 };

const WW = 48, WH = 150, BW = 30, BH = 95;
const rects = []; // {x,y,w,h,semis,black}

function buildPiano() {
  const nWhite = WHITE.length;
  piano.width = nWhite * WW;
  piano.height = WH;
  rects.length = 0;
  WHITE.forEach((semis, i) => {
    rects.push({ x: i * WW, y: 0, w: WW, h: WH, semis, black: false });
  });
  Object.keys(BLACK).map(Number).forEach((semis) => {
    const wi = BLACK[semis];
    rects.push({ x: (wi + 1) * WW - BW / 2, y: 0, w: BW, h: BH, semis, black: true });
  });
  drawPiano();
}

function drawPiano() {
  const g = piano.getContext("2d");
  g.clearRect(0, 0, piano.width, piano.height);
  // whites
  rects.filter((r) => !r.black).forEach((r) => {
    const on = held.has(baseNote + r.semis);
    g.fillStyle = on ? "#7fd1ff" : "#f7f7f5";
    g.fillRect(r.x, r.y, r.w - 1, r.h);
    g.strokeStyle = "#222"; g.lineWidth = 1; g.strokeRect(r.x, r.y, r.w - 1, r.h);
    g.fillStyle = "#555"; g.font = "10px monospace";
    g.fillText(`M${baseNote + r.semis}`, r.x + 4, r.h - 6);
  });
  // blacks
  rects.filter((r) => r.black).forEach((r) => {
    const on = held.has(baseNote + r.semis);
    g.fillStyle = on ? "#39c0ff" : "#1a1a1a";
    g.fillRect(r.x, r.y, r.w, r.h);
  });
}

function semisAt(x, y) {
  // blacks on top
  const bl = rects.find((r) => r.black && x >= r.x && x <= r.x + r.w && y <= r.h);
  if (bl) return bl.semis;
  const wh = rects.find((r) => !r.black && x >= r.x && x <= r.x + r.w && y <= r.h);
  return wh ? wh.semis : null;
}

function press(semis) {
  const n = baseNote + semis;
  if (held.has(n)) return;
  held.add(n);
  post({ type: "noteOn", midi: n, vel: 0.9 });
  drawPiano();
}
function release(semis) {
  const n = baseNote + semis;
  if (!held.has(n)) return;
  held.delete(n);
  post({ type: "noteOff", midi: n });
  drawPiano();
}

let mouseSemis = null;
piano.addEventListener("mousedown", (e) => {
  const r = piano.getBoundingClientRect();
  const s = semisAt(e.clientX - r.left, e.clientY - r.top);
  if (s !== null) { mouseSemis = s; press(s); }
});
window.addEventListener("mouseup", () => {
  if (mouseSemis !== null) { release(mouseSemis); mouseSemis = null; }
});

// computer keyboard
window.addEventListener("keydown", (e) => {
  if (e.repeat) return;
  const k = e.key.toLowerCase();
  if (k === "z") { baseNote = Math.max(0, baseNote - 12); drawPiano(); return; }
  if (k === "x") { baseNote = Math.min(108, baseNote + 12); drawPiano(); return; }
  if (k in KEYMAP) { e.preventDefault(); press(KEYMAP[k]); }
});
window.addEventListener("keyup", (e) => {
  const k = e.key.toLowerCase();
  if (k in KEYMAP) release(KEYMAP[k]);
});

$("allOff").addEventListener("click", () => {
  held.clear(); post({ type: "allOff" }); drawPiano();
});

// ---- scope -----------------------------------------------------------------
function drawScope() {
  requestAnimationFrame(drawScope);
  const g = scope.getContext("2d");
  const w = scope.width, h = scope.height;
  g.fillStyle = "#0d1117"; g.fillRect(0, 0, w, h);
  if (!analyser) return;
  const buf = new Uint8Array(analyser.frequencyBinCount);
  analyser.getByteTimeDomainData(buf);
  g.lineWidth = 2; g.strokeStyle = "#39c0ff"; g.beginPath();
  for (let i = 0; i < buf.length; i++) {
    const x = (i / buf.length) * w;
    const y = (buf[i] / 255) * h;
    if (i === 0) g.moveTo(x, y); else g.lineTo(x, y);
  }
  g.stroke();
}

// ---- boot ------------------------------------------------------------------
async function startAudio() {
  startBtn.disabled = true;
  startBtn.textContent = "Starting…";
  try {
    ctx = new (window.AudioContext || window.webkitAudioContext)();
    await ctx.resume();
    await ctx.audioWorklet.addModule("vb1-worklet.js");

    node = new AudioWorkletNode(ctx, "vb1-processor", { outputChannelCount: [2] });
    // send the real tables + program map
    node.port.postMessage({ type: "init", tables: EXC_TABLES, programTable: PROGRAM_TABLE });

    analyser = ctx.createAnalyser();
    analyser.fftSize = 2048;
    node.connect(analyser);
    analyser.connect(ctx.destination);

    // push the initial program (0) so the worklet has params + table
    const [, vals] = PRESETS[0];
    post({ type: "program", program: 0, params: paramObj(vals) });

    startOverlay.style.display = "none";
    studio.style.display = "block";
    srEl.textContent = `${ctx.sampleRate} Hz`;
    statusEl.textContent = "ready — play with mouse or keyboard";
    drawScope();
  } catch (err) {
    console.error(err);
    startBtn.disabled = false;
    startBtn.textContent = "Start";
    statusEl.textContent = "Audio init failed: " + err.message;
  }
}

startBtn.addEventListener("click", startAudio);
buildPiano();
