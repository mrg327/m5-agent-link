// "sound" chapter: audition the watch's notification sounds. The bank is firmware data
// (sound_bank.c → facts.sounds) rendered by the same chip synth the watch will run.
import { h, svg, bodySlot } from '../gestures/dom.js';
import { render, hold, durationMs, repeat } from './synth.js';

const PICKS_KEY = 'atlas.sound.picks';
const ROLE_COLOR = { done: 'var(--c-success)', input: 'var(--c-accent)' };
const WAVE_LABEL = { sq12: 'pulse 12%', sq25: 'pulse 25%', sq50: 'square', tri: 'triangle', saw: 'saw', noise: 'noise', sine: 'sine', fm: 'FM piano', bell: 'FM bell' };
const steps = (s) => s.tracks.flat();

let audio = null;
function ctxAudio() {
  const AC = globalThis.AudioContext || globalThis.webkitAudioContext;
  if (!AC) return null;
  audio ||= new AC();
  if (audio.state === 'suspended') audio.resume();
  return audio;
}

function loadPicks() {
  try { return JSON.parse(localStorage.getItem(PICKS_KEY)) || {}; } catch { return {}; }
}

// Note roll: time across, pitch up; slides drawn as ramps, rests as gaps, echo as a fading tail.
function roll(sound, lo, hi) {
  const total = durationMs(sound);
  const y = (n) => 90 - (Math.min(hi, Math.max(lo, n)) - lo) / Math.max(1, hi - lo) * 80;
  const shapes = [];
  const echo = sound.echo?.ms ? sound.echo : null;
  for (const track of sound.tracks) {
    let t = 0;
    for (const s of track) {
      if (s.note && s.vol) {
        const end = s.slide || s.note;
        const pts = (dt) => `${t + dt},${y(s.note) - 4} ${t + s.ms + dt},${y(end) - 4} ${t + s.ms + dt},${y(end) + 4} ${t + dt},${y(s.note) + 4}`;
        const a = (s.vol / 15) * (s.decay ? 0.55 : 1);
        const noise = s.wave === 'noise' ? '1' : null;
        shapes.push(svg('polygon', { points: pts(0), 'fill-opacity': a, 'data-noise': noise }));
        for (let k = 1; echo && k <= 3; k++) {
          shapes.push(svg('polygon', { points: pts(k * echo.ms), 'fill-opacity': a * (echo.pct / 100) ** k, 'data-noise': noise }));
        }
      }
      t += s.ms;
    }
  }
  return svg('svg', { class: 'snd__roll', viewBox: `0 0 ${total} 100`, preserveAspectRatio: 'none', 'aria-hidden': 'true' }, shapes);
}

export function mount(section, ctx) {
  const bank = ctx.facts.sounds;
  const root = h('div', { class: 'snd' });
  bodySlot(section).append(root);
  if (!bank?.bank?.length) {
    root.append(h('p', { text: 'No sound bank in facts.json. Run python3 atlas/tools/extract.py.' }));
    return;
  }

  const notes = bank.bank.flatMap((s) => steps(s).filter((x) => x.wave !== 'noise').flatMap((x) => [x.note, x.slide])).filter(Boolean);
  const lo = Math.min(...notes) - 2, hi = Math.max(...notes) + 2;
  const picks = { ...Object.fromEntries(bank.roles.map((r) => [r.id, r.pick])), ...loadPicks() };
  let small = true, volume = 0.8, playing = null;

  function play(sound, card) {
    const ac = ctxAudio();
    if (!ac) return;
    playing?.src.stop();
    const role = bank.roles.find((r) => r.id === sound.role) || {};
    const cue = repeat(render(sound, bank.rate), role.repeat, role.gap_ms, bank.rate);
    const pcm = hold(cue, bank.rate, ac.sampleRate);
    const buf = ac.createBuffer(1, pcm.length, ac.sampleRate);
    buf.getChannelData(0).set(pcm);
    const src = ac.createBufferSource();
    src.buffer = buf;
    const gain = ac.createGain();
    gain.gain.value = volume;
    let node = src;
    if (small) {
      // A 1 W speaker in a watch case: little below ~600 Hz, rolls off up top.
      const hp = ac.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = 600; hp.Q.value = 0.9;
      const lp = ac.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 7000;
      node.connect(hp); hp.connect(lp); node = lp;
    }
    node.connect(gain); gain.connect(ac.destination);
    card.style.setProperty('--dur', `${(cue.length * 1000) / bank.rate}ms`);
    card.dataset.playing = '';
    void card.offsetWidth;
    card.dataset.playing = '1';
    src.onended = () => { if (playing?.src === src) { card.dataset.playing = ''; playing = null; } };
    playing = { src, card };
    src.start();
  }

  const summary = h('p', { class: 'snd__summary', role: 'status' });
  const showPicks = () => {
    const parts = bank.roles.map((r) => {
      const s = bank.bank.find((x) => x.id === picks[r.id]);
      return h('span', {}, h('b', { text: r.id }), ' ', s ? s.name : '—');
    });
    summary.replaceChildren('Your picks: ', ...parts.flatMap((p, i) => (i ? [' · ', p] : [p])));
  };

  const smallBtn = h('button', { type: 'button', class: 'snd__toggle', 'aria-pressed': 'true', text: 'Watch speaker' });
  smallBtn.addEventListener('click', () => { small = !small; smallBtn.setAttribute('aria-pressed', String(small)); });
  const vol = h('input', { type: 'range', min: 0, max: 1, step: 0.05, value: volume, 'aria-label': 'Volume' });
  vol.addEventListener('input', () => { volume = Number(vol.value); });

  root.append(
    h('p', { class: 'snd__lede' }, 'Short game cues, from 8-bit blips to FM chimes and layered fanfares: square, triangle, saw, ',
      `noise, sine and FM voices, up to ${Math.max(...bank.bank.map((s) => s.tracks.length))} at once, with echo, at ${bank.rate / 1000} kHz. `,
      'Press a card to hear it and pick one for each moment. ',
      h('i', { text: 'Watch speaker' }), ' approximates the little speaker; turn it off to hear the raw synth.'),
    h('div', { class: 'snd__bar' }, smallBtn, h('label', { class: 'snd__vol' }, 'Volume ', vol)),
  );

  for (const role of bank.roles) {
    const sounds = bank.bank.filter((s) => s.role === role.id);
    const grid = h('div', { class: 'snd__grid', role: 'radiogroup', 'aria-label': `Pick the ${role.id} sound` });
    for (const s of sounds) {
      const waves = [...new Set(steps(s).filter((x) => x.note).map((x) => WAVE_LABEL[x.wave] || x.wave))].join(', ');
      const extra = [s.tracks.length > 1 && `${s.tracks.length} voices`, s.echo?.ms && 'echo'].filter(Boolean);
      const card = h('div', { class: 'snd__card', style: { '--c': ROLE_COLOR[role.id] || 'var(--c-amber)' } });
      const btn = h('button', { type: 'button', class: 'snd__play', 'aria-label': `Play ${s.name}` },
        roll(s, lo, hi),
        h('span', { class: 'snd__name', text: s.name }),
        h('span', { class: 'snd__meta mono', text: [`${durationMs(s)} ms`, waves, ...extra].join(' · ') }));
      btn.addEventListener('click', () => play(s, card));
      const radio = h('input', { type: 'radio', name: `snd-pick-${role.id}`, value: s.id, checked: picks[role.id] === s.id });
      radio.addEventListener('change', () => {
        picks[role.id] = s.id;
        try { localStorage.setItem(PICKS_KEY, JSON.stringify(picks)); } catch { /* private mode */ }
        showPicks();
        play(s, card);
      });
      const chosen = role.pick === s.id ? h('span', { class: 'snd__chosen', text: 'On the watch' }) : null;
      card.append(btn, h('label', { class: 'snd__pick' }, radio, ' Pick', chosen));
      grid.append(card);
    }
    root.append(h('section', { class: 'snd__role', 'aria-labelledby': `snd-role-${role.id}` },
      h('h3', { class: 'snd__role-t', id: `snd-role-${role.id}` }, h('span', { class: 'mono', text: role.id }), ' ', role.when,
        role.repeat > 1 ? ` (plays ${role.repeat}×, ${role.gap_ms} ms apart)` : ''),
      grid));
  }
  showPicks();
  root.append(summary,
    h('p', { class: 'snd__note' }, 'Sounds live in ', h('code', { text: 'firmware/components/core/src/sound_bank.c' }),
      '. Add a candidate there, run ', h('code', { text: 'python3 atlas/tools/extract.py' }), ' and it shows up here. ',
      'The watch plays the role\'s ', h('code', { text: 'pick' }), ' from ', h('code', { text: 'snd_roles' }), '; picks on this page stay in your browser.'));
}

export function onLeave() {
  if (audio?.state === 'running') audio.suspend();
}
