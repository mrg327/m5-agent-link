// Markup for the StopWatch render. Geometry is in a 600×600 box (≈11.5 px/mm:
// body Ø52 mm, 1.75" AMOLED, 15.5 mm thick; see docs/hardware/StopWatch_model_size.pdf).
// Orange key = A, coral key = B, both on the lower rim; USB-C on the right edge.

export const SLABS = 26;
export const SLAB_STEP = 5;                 // px between slabs → ≈130 px thickness
export const DEPTH = SLABS * SLAB_STEP;

export const REGIONS = {
  screen:  { label: 'AMOLED · 466×466',  holes: [{ x: 300, y: 300, r: 262 }], tag: { x: 300, y: 300 } },
  buttons: { label: 'Keys A · B',        holes: [{ x: 98, y: 508, r: 58 }, { x: 502, y: 508, r: 58 }], tag: { x: 300, y: 590 } },
  motor:   { label: 'Vibration motor',   holes: [{ x: 300, y: 505, r: 70 }], tag: { x: 300, y: 505 }, inside: true },
  usb:     { label: 'USB-C',             holes: [{ x: 598, y: 300, r: 62 }], tag: { x: 598, y: 300 } },
  antenna: { label: '2.4 GHz antenna',   holes: [{ x: 300, y: 62, r: 78 }],  tag: { x: 300, y: 62 }, inside: true },
};

export function slabsHTML() {
  let s = '';
  for (let i = 0; i < SLABS; i++) {
    // the two lowest slabs fade to the "back" colour; the rest share one anisotropic band
    s += `<i class="slab" style="transform:translateZ(${-(i + 1) * SLAB_STEP}px)"></i>`;
  }
  return s;
}

export function faceSVG() {
  return `
<svg class="face__svg" viewBox="0 0 600 600" aria-hidden="true" focusable="false">
  <defs>
    <radialGradient id="g-body" cx="34%" cy="26%" r="95%">
      <stop offset="0" stop-color="#2b2e36"/><stop offset=".35" stop-color="#15171c"/>
      <stop offset=".75" stop-color="#0a0b0e"/><stop offset="1" stop-color="#040405"/>
    </radialGradient>
    <linearGradient id="g-chamfer" x1="15%" y1="8%" x2="85%" y2="95%">
      <stop offset="0" stop-color="#7b8190" stop-opacity=".95"/>
      <stop offset=".22" stop-color="#3a3e48"/><stop offset=".55" stop-color="#101216"/>
      <stop offset=".85" stop-color="#2a2d35"/><stop offset="1" stop-color="#4b505c" stop-opacity=".9"/>
    </linearGradient>
    <linearGradient id="g-lip" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#000"/><stop offset=".6" stop-color="#16181d"/><stop offset="1" stop-color="#3b3f49"/>
    </linearGradient>
    <radialGradient id="g-glass" cx="50%" cy="50%" r="50%">
      <stop offset="0" stop-color="#05060a"/><stop offset=".9" stop-color="#010102"/><stop offset="1" stop-color="#000"/>
    </radialGradient>
    <filter id="f-soft" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="1.4"/></filter>
  </defs>
  <circle cx="300" cy="300" r="300" fill="url(#g-body)"/>
  <circle cx="300" cy="300" r="297.5" fill="none" stroke="url(#g-chamfer)" stroke-width="5"/>
  <circle cx="300" cy="300" r="283" fill="none" stroke="#000" stroke-opacity=".55" stroke-width="2"/>
  <circle cx="300" cy="300" r="270" fill="none" stroke="url(#g-lip)" stroke-width="9"/>
  <circle cx="300" cy="300" r="262" fill="url(#g-glass)"/>
  <circle cx="300" cy="300" r="262" fill="none" stroke="#fff" stroke-opacity=".09" stroke-width="1"/>
</svg>`;
}

export function glassSVG() {
  return `
<svg class="glass__svg" viewBox="0 0 600 600" aria-hidden="true" focusable="false">
  <defs>
    <clipPath id="c-glass"><circle cx="300" cy="300" r="262"/></clipPath>
    <linearGradient id="g-sheen" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#fff" stop-opacity=".16"/><stop offset=".5" stop-color="#fff" stop-opacity=".02"/>
      <stop offset="1" stop-color="#fff" stop-opacity="0"/>
    </linearGradient>
    <linearGradient id="g-spec" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#fff" stop-opacity=".85"/><stop offset=".5" stop-color="#cfe0ff" stop-opacity=".18"/>
      <stop offset="1" stop-color="#fff" stop-opacity="0"/>
    </linearGradient>
    <radialGradient id="g-fresnel" cx="50%" cy="50%" r="50%">
      <stop offset=".86" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity=".5"/>
    </radialGradient>
    <radialGradient id="g-bloom" cx="30%" cy="22%" r="60%">
      <stop offset="0" stop-color="#bcd0ff" stop-opacity=".10"/><stop offset="1" stop-color="#bcd0ff" stop-opacity="0"/>
    </radialGradient>
    <filter id="f-blur2"><feGaussianBlur stdDeviation="2.2"/></filter>
    <filter id="f-blur14"><feGaussianBlur stdDeviation="14"/></filter>
  </defs>
  <g clip-path="url(#c-glass)">
    <circle cx="300" cy="300" r="262" fill="url(#g-bloom)"/>
    <g class="glass__sheen">
      <path d="M-40 130 L520 -40 L640 60 L-10 330 Z" fill="url(#g-sheen)"/>
      <path d="M-40 400 L420 640 L330 700 L-40 520 Z" fill="#fff" fill-opacity=".025" filter="url(#f-blur14)"/>
    </g>
    <circle cx="300" cy="300" r="262" fill="url(#g-fresnel)"/>
  </g>
  <path d="M96 205 A218 218 0 0 1 318 84" fill="none" stroke="url(#g-spec)" stroke-width="3.2" stroke-linecap="round" filter="url(#f-blur2)" opacity=".9"/>
  <path d="M104 196 A210 210 0 0 1 246 107" fill="none" stroke="#fff" stroke-opacity=".7" stroke-width="1.1" stroke-linecap="round"/>
  <path d="M520 420 A236 236 0 0 1 430 508" fill="none" stroke="#9fb6ff" stroke-opacity=".28" stroke-width="2" stroke-linecap="round" filter="url(#f-blur2)"/>
</svg>`;
}

function keyLayers() {
  // a key is a short stack of planes so it still has body when the puck tilts
  let s = '';
  for (let i = 0; i < 7; i++) s += `<i style="transform:translateZ(${-(52 + i * 4)}px)"></i>`;
  return s;
}

export function usbHTML() {
  return `<div class="usb" aria-hidden="true"><div class="usb__shell"><div class="usb__slot"><i class="usb__tongue"></i></div></div></div>`;
}

export function deviceHTML() {
  return `
<div class="stage__scene">
  <div class="stage__shadow" aria-hidden="true"></div>
  <div class="device" data-device>
    ${slabsHTML()}
    ${usbHTML()}
    <button class="key key--a" type="button" data-key="a" aria-label="Button A (keyboard: A)" title="Button A · key A">${keyLayers()}</button>
    <button class="key key--b" type="button" data-key="b" aria-label="Button B (keyboard: B)" title="Button B · key B">${keyLayers()}</button>
    <div class="face">
      ${faceSVG()}
      <div class="face__sheen" aria-hidden="true"></div>
      <div class="screen">
        <canvas class="screen__canvas" width="466" height="466" aria-label="Live StopWatch screen"></canvas>
        <canvas class="screen__trace" width="466" height="466" aria-hidden="true"></canvas>
        <div class="screen__annos" aria-live="polite"></div>
      </div>
      <div class="glass">${glassSVG()}</div>
      <svg class="spot" viewBox="-100 -100 800 800" aria-hidden="true" focusable="false">
        <defs>
          <mask id="m-spot" maskUnits="userSpaceOnUse" x="-100" y="-100" width="800" height="800">
            <rect x="-100" y="-100" width="800" height="800" fill="#fff"/>
            <g class="spot__holes" fill="#000" filter="url(#f-spot)"></g>
          </mask>
          <filter id="f-spot" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="14"/></filter>
        </defs>
        <circle class="spot__dim" cx="300" cy="300" r="302" fill="#05060a" mask="url(#m-spot)"/>
        <g class="spot__ring"></g>
      </svg>
      <div class="spot__label" aria-hidden="true"></div>
    </div>
    <svg class="fx" viewBox="-100 -100 800 800" aria-hidden="true" focusable="false"></svg>
  </div>
</div>`;
}
