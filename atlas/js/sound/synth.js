// Synth for the watch's sound bank. Follows the contract in
// firmware/components/core/src/sound_bank.h so what you hear here is what the watch will play.
// Pure functions: no DOM, no Web Audio, so node --test can check them.

export const midiHz = (n) => 440 * 2 ** ((n - 69) / 12);

const DUTY = { sq12: 0.125, sq25: 0.25, sq50: 0.5 };
const FM_OP = { fm: { ratio: 2, index: 2.5 }, bell: { ratio: 3.5, index: 3 } };
const TAU = 2 * Math.PI;
const samples = (ms, rate) => Math.round((ms * rate) / 1000);
const trackMs = (steps) => steps.reduce((t, s) => t + s.ms, 0);

export function durationMs(sound) {
  return Math.max(...sound.tracks.map(trackMs)) + (sound.echo?.ms ? 3 * sound.echo.ms : 0);
}

function renderTrack(steps, out, rate) {
  let phase = 0, mphase = 0, nphase = 0, lfsr = 1, i0 = 0;
  for (const s of steps) {
    const len = samples(s.ms, rate);
    const ramp = Math.max(1, Math.min(Math.round(0.002 * rate), Math.floor(len / 4)));
    const amp = (s.vol / 15) * 0.5;
    const op = FM_OP[s.wave];
    for (let i = 0; i < len; i++) {
      if (!s.note || !amp) continue;
      const n = s.slide ? s.note + ((s.slide - s.note) * i) / len : s.note;
      const inc = midiHz(n) / rate;
      const env = (s.decay ? 1 - i / len : 1) * Math.min(1, i / ramp, (len - i) / ramp);
      let v;
      if (s.wave === 'noise') {
        nphase += 16 * inc;
        while (nphase >= 1) {
          nphase -= 1;
          const fb = (lfsr ^ (lfsr >> 1)) & 1;
          lfsr = (lfsr >> 1) | (fb << 14);
        }
        v = lfsr & 1 ? -1 : 1;
      } else if (s.wave === 'tri') v = 4 * Math.abs(phase - 0.5) - 1;
      else if (s.wave === 'saw') v = 2 * phase - 1;
      else if (s.wave === 'sine') v = Math.sin(TAU * phase);
      else if (op) {
        v = Math.sin(TAU * phase + op.index * env * Math.sin(TAU * mphase));
        mphase = (mphase + op.ratio * inc) % 1;
      } else v = phase < DUTY[s.wave] ? 1 : -1;
      phase = (phase + inc) % 1;
      out[i0 + i] += v * amp * env;
    }
    i0 += len;
  }
}

// Render a sound to Float32 PCM at `rate` (the bank's rate, 16 kHz).
export function render(sound, rate = 16000) {
  const out = new Float32Array(samples(durationMs(sound), rate));
  for (const t of sound.tracks) renderTrack(t, out, rate);
  const d = samples(sound.echo?.ms || 0, rate);
  if (d) {
    const fb = sound.echo.pct / 100;
    for (let i = d; i < out.length; i++) out[i] += fb * out[i - d];
    for (let i = out.length - d; i < out.length; i++) out[i] *= (out.length - i) / d;
  }
  for (let i = 0; i < out.length; i++) out[i] = Math.max(-1, Math.min(1, out[i]));
  return out;
}

// A role's cue: the pick played `times` times with `gapMs` of silence between (snd_role_t).
export function repeat(pcm, times = 1, gapMs = 0, rate = 16000) {
  const gap = samples(gapMs, rate), n = Math.max(1, times);
  const out = new Float32Array(n * pcm.length + (n - 1) * gap);
  for (let k = 0; k < n; k++) out.set(pcm, k * (pcm.length + gap));
  return out;
}

// Sample-and-hold upsample to the AudioContext rate (as a plain DAC would), so every browser
// accepts the buffer whatever rates it allows for createBuffer.
export function hold(pcm, from, to) {
  const n = Math.round((pcm.length * to) / from);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = pcm[Math.min(pcm.length - 1, Math.floor((i * from) / to))];
  return out;
}
