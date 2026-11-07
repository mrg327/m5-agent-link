// Hero: the watch plays a demo scenario while the page waits for a first touch.
export function mount(section, ctx) {
  const caption = section.querySelector('#hero-caption');
  const replay = section.querySelector('#hero-replay');
  const led = section.querySelector('.hero__led');

  ctx.bus.on('scenario:step', (s) => { if (s?.text) caption.textContent = s.text; });
  ctx.bus.on('watch:screen', () => { led.dataset.live = '1'; });

  const scenarios = ctx.daemon?.scenarios ?? [];
  const demo = scenarios.find((s) => /demo|hero|intro|day/i.test(`${s.name} ${s.title}`)) ?? scenarios[0];
  const start = () => { if (demo) ctx.daemon.play(demo.name); };

  if (demo) {
    caption.textContent = demo.title ?? 'Demo running';
    start();
  } else {
    caption.textContent = ctx.watch.kind === 'stub'
      ? 'Watch preview only: the firmware build is not loaded yet.'
      : 'Tap the watch to wake it.';
    replay.hidden = true;
  }
  replay.addEventListener('click', () => { ctx.watch.reset?.(); start(); });
}
