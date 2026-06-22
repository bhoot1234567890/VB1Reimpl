// =============================================================================
// vb1-worklet.js — AudioWorklet DSP for the VB-1 waveguide.
//
// Faithful port of Source/VB1DSP.h (VaStringVoice). Runs per-sample on the
// audio thread. The 11 real excitation tables are sent from the main thread
// (parsed from the binary dump) via port 'init' -- see app.js.
//
// Message protocol (main -> worklet):
//   {type:'init',     tables: Float32Array[11], programTable: number[16]}
//   {type:'program',  program: number, params: {damper,pickup,pick,release,shape,volume}}
//   {type:'param',    name: string, value: number}
//   {type:'noteOn',   midi: number, vel: number}
//   {type:'noteOff',  midi: number}
//   {type:'allOff'}
// =============================================================================
"use strict";

const K_NUM_VOICES = 8;
const K_MAX_DELAY  = 9599;     // N clamp (binary 0x257f)
const K_OUT_GAIN   = 0.7795;   // binary runtime-dumped output gain

// --- one note's waveguide ---------------------------------------------------
class VB1Voice {
  constructor(sampleRate) {
    this.sr = sampleRate;
    this.N = 256;
    this.pk = 0;
    this.r = 1;
    this.g = 0.0;
    this.idxA = 0;
    this.idxB = 0;
    this.dlA = new Float32Array(1);
    this.dlB = new Float32Array(1);
    this.filt = 0.0;
    this.env = 1.0;
    this.releaseDec = 0.0;
    this.releasing = false;
    this.gainL = 0.0;
    this.gainR = 0.0;
    this.midi = -1;
    this.vel = 1.0;
    this.active = false;
  }

  midiToFreq(m, damper) {
    // f = 880 * 2^((m-69)/12 + delta); delta = (0.5-D)/18 when D<0.5
    let octave = (m - 69) / 12;
    if (damper < 0.5) octave += (0.5 - damper) * (1 / 12) * (2 / 3);
    return (2 * 440) * Math.pow(2, octave);
  }

  startNote(midi, vel, params, table) {
    this.params = params;
    this.vel = vel;
    this.midi = midi;

    const f = this.midiToFreq(midi, params.damper);
    this.N = Math.min(Math.floor(this.sr / f + 1.0), K_MAX_DELAY);

    // g = 1 - min(0.9, (float)((9600/N)*0.0125*(0.8D+0.1)))
    // Math.fround replicates the C++ (float) single-precision cast.
    const gcoeff = Math.fround((9600.0 / this.N) * 0.0125 * (0.8 * params.damper + 0.1));
    this.g = 1.0 - Math.min(0.9, gcoeff);

    this.pk = Math.max(0, Math.min(this.N - 1,
      Math.floor((params.pickup / 6.0 + 1 / 64.0) * this.N)));
    this.r = Math.max(1, Math.floor(params.pick * 0.5 * this.N));

    this._seed(table);

    this.gainL = 0.5 * params.volume * vel * K_OUT_GAIN;
    this.gainR = 0.5 * params.volume * vel * K_OUT_GAIN;

    this.idxA = 0;
    this.idxB = 0;
    this.filt = 0.0;
    this.env = 1.0;
    this.releasing = false;
    this.releaseDec = 0.0;
    this.active = true;
  }

  _seed(table) {
    const N = this.N, r = this.r;
    const dlA = new Float32Array(N);
    const dlB = new Float32Array(N);
    const rise = 1.0 / r;
    const fall = 1.0 / Math.max(1, N - 1 - r);
    const step = 4096.0 / N;
    let phi = 0.0;
    for (let i = 0; i < r; i++) {
      const tv = table[Math.floor(phi) & 0xFFF];
      const f = tv * rise * i;
      const v = (f + f) - 0.1;
      dlA[i] = v; dlB[i] = v;
      phi += step;
    }
    for (let i = r; i < N; i++) {
      const tv = table[Math.floor(phi) & 0xFFF];
      const f = tv * fall * (N - 1 - i);
      const v = (f + f) - 0.1;
      dlA[i] = v; dlB[i] = v;
      phi += step;
    }
    this.dlA = dlA;
    this.dlB = dlB;
  }

  stopNote() {
    if (!this.active) return;
    // FUN_0001e630: g switches to Release, linear env 1 -> 0.
    this.releasing = true;
    this.g = this.params.release;
    let rel = Math.floor(this.sr * 2.5 * (1.0 - this.params.release));
    rel = Math.max(256, rel);
    this.releaseDec = 1.0 / rel;
    this.env = 1.0;
  }

  setVolume(v) {
    this.gainL = 0.5 * v * this.vel * K_OUT_GAIN;
    this.gainR = this.gainL;
  }

  // Sum n samples into stereo buffers (Float32Array each, length n).
  renderInto(outL, outR, n) {
    if (!this.active) return;
    const N = this.N, dlA = this.dlA, dlB = this.dlB, pk = this.pk;
    const g = this.g, om = 1.0 - g;
    const gainL = this.gainL, gainR = this.gainR;
    const releasing = this.releasing, dec = this.releaseDec;
    let idxA = this.idxA, idxB = this.idxB, filt = this.filt, env = this.env;

    for (let s = 0; s < n; s++) {
      let pi = idxB + pk; if (pi >= N) pi -= N;
      const pick = dlB[pi];

      filt = g * filt + om * dlB[idxB];
      dlA[idxA] = -filt;

      let ap = idxA + N - 1; if (ap >= N) ap -= N;
      dlB[idxB] = -dlA[ap];

      idxA--; if (idxA < 0) idxA += N;
      idxB++; if (idxB >= N) idxB -= N;

      const e = env;
      outL[s] += pick * e * gainL;
      outR[s] += pick * e * gainR;

      if (releasing) {
        env -= dec;
        if (env <= 0.0) { this.active = false; break; }
      }
    }

    this.idxA = idxA;
    this.idxB = idxB;
    this.filt = filt;
    this.env = env;
  }
}

// --- 8-voice engine ---------------------------------------------------------
class VB1Processor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.voices = [];
    for (let i = 0; i < K_NUM_VOICES; i++) this.voices.push(new VB1Voice(sampleRate));
    this.params = { damper: 0.1, pickup: 0.75, pick: 0.333, release: 0.98, shape: 0, volume: 0.8 };
    this.tables = null;       // Float32Array[11]
    this.programTable = null; // number[16]
    this.table = null;        // current program's table
    this.noteMap = new Map(); // midi -> voice index
    this.steal = 0;
    this.port.onmessage = (ev) => this._onMessage(ev.data);
  }

  _onMessage(msg) {
    switch (msg.type) {
      case "init":
        this.tables = msg.tables;
        this.programTable = msg.programTable;
        this.table = this.tables[this.programTable[0]];
        break;
      case "program":
        this.params = Object.assign({}, msg.params);
        this.table = this.tables[this.programTable[msg.program]];
        for (const v of this.voices) if (v.active) { v.params = this.params; v.setVolume(this.params.volume); }
        break;
      case "param":
        this.params[msg.name] = msg.value;
        if (msg.name === "volume")
          for (const v of this.voices) if (v.active) v.setVolume(msg.value);
        break;
      case "noteOn":  this._alloc(msg.midi, msg.vel); break;
      case "noteOff": {
        const vi = this.noteMap.get(msg.midi);
        if (vi !== undefined && this.voices[vi].active) this.voices[vi].stopNote();
        this.noteMap.delete(msg.midi);
        break;
      }
      case "allOff":
        for (const v of this.voices) if (v.active && !v.releasing) v.stopNote();
        break;
    }
  }

  _alloc(midi, vel) {
    if (!this.table) return; // not initialized yet
    if (this.noteMap.has(midi)) {
      const i = this.noteMap.get(midi);
      this.voices[i].startNote(midi, vel, this.params, this.table);
      return;
    }
    for (let i = 0; i < this.voices.length; i++) {
      if (!this.voices[i].active) { this._start(i, midi, vel); return; }
    }
    this.steal = (this.steal + 1) % this.voices.length;
    this._start(this.steal, midi, vel);
  }

  _start(i, midi, vel) {
    this.voices[i].startNote(midi, vel, this.params, this.table);
    this.noteMap.set(midi, i);
  }

  process(_inputs, outputs) {
    const out = outputs[0];
    if (!out || out.length === 0) return true;
    const L = out[0];
    const R = out.length > 1 ? out[1] : L;
    const n = L.length;
    for (let i = 0; i < n; i++) { L[i] = 0; if (R !== L) R[i] = 0; }

    if (this.table) {
      for (const v of this.voices) if (v.active) v.renderInto(L, R, n);
    }

    // master gain + soft-clip (tanh) limiter
    const mg = 0.7;
    for (let i = 0; i < n; i++) {
      L[i] = Math.tanh(L[i] * mg);
      if (R !== L) R[i] = Math.tanh(R[i] * mg);
    }
    return true;
  }
}

registerProcessor("vb1-processor", VB1Processor);
