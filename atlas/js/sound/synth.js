// Chip synth for the watch's sound bank. Follows the contract in
// firmware/components/core/src/sound_bank.h so what you hear here is what the watch will play.
// Pure functions: no DOM, no Web Audio, so node --test can check them.

export const midiHz = (n) => 440 * 2 ** ((n - 69) / 12);

const DUTY = { sq12: 0.125, sq25: 0.25, sq50: 0.5 };

export function durationMs(sound) {
  return sound.steps.reduce((t, s) => t + s.ms, 0);
}

// Render a sound to Float32 PCM at `rate` (the bank's rate, 16 kHz).
export function render(sound, rate = 16000) {
  const total = sound.steps.reduce((n, s) => n + Math.round((s.ms * rate) / 1000), 0);
  const out = new Float32Array(total);
  let phase = 0, nphase = 0, lfsr = 1, i0 = 0;
  for (const s of sound.steps) {
    const len = Math.round((s.ms * rate) / 1000);
    const ramp = Math.max(1, Math.min(Math.round(0.002 * rate), Math.floor(len / 4)));
    const amp = (s.vol / 15) * 0.5;
    for (let i = 0; i < len; i++) {
      if (!s.note || !amp) { out[i0 + i] = 0; continue; }
      const n = s.slide ? s.note + ((s.slide - s.note) * i) / len : s.note;
      const inc = midiHz(n) / rate;
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
      else v = phase < DUTY[s.wave] ? 1 : -1;
      phase = (phase + inc) % 1;
      let env = s.decay ? 1 - i / len : 1;
      env *= Math.min(1, i / ramp, (len - i) / ramp);
      out[i0 + i] = v * amp * env;
    }
    i0 += len;
  }
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
