(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const selector = $('gamepads');
  const tested = new Set();
  const visualButtons = [...document.querySelectorAll('[data-button]')];
  const chips = [...document.querySelectorAll('[data-chip]')];
  let selected = null;
  let previousDevices = '';

  function reset() { tested.clear(); updateTested(); }
  function updateTested() {
    $('tested-count').textContent = tested.size;
    $('progress-bar').style.width = `${tested.size / 8 * 100}%`;
    chips.forEach((chip, i) => chip.classList.toggle('tested', tested.has(i)));
  }
  $('reset').addEventListener('click', reset);
  selector.addEventListener('change', () => { selected = Number(selector.value); reset(); });
  const safeAxis = value => Number.isFinite(value) ? Math.max(-1, Math.min(1, value)) : 0;
  function updateDevices(pads) {
    const signature = JSON.stringify(pads.map(p => [p.index, p.id]));
    if (signature === previousDevices) return;
    previousDevices = signature;
    if (!pads.some(p => p.index === selected)) { selected = pads.length ? pads[0].index : null; reset(); }
    selector.replaceChildren();
    if (!pads.length) {
      selector.add(new Option('No gamepad detected', ''));
      selector.disabled = true;
    } else {
      pads.forEach(p => selector.add(new Option(p.id || `Gamepad ${p.index + 1}`, String(p.index))));
      selector.value = String(selected);
      selector.disabled = false;
    }
  }
  function render(pad) {
    const connected = !!pad;
    $('connection-title').textContent = connected ? 'Gamepad connected' : 'Waiting for a gamepad';
    $('connection-detail').textContent = connected ? pad.id : 'Pair over Bluetooth, then press any button on your controller.';
    $('status').classList.toggle('connected', connected);
    $('status').lastChild.textContent = connected ? ' CONNECTED' : ' WAITING';
    const x = connected ? safeAxis(pad.axes[0]) : 0;
    const y = connected ? safeAxis(pad.axes[1]) : 0;
    $('axis-x').textContent = connected && pad.axes.length > 0 ? x.toFixed(2) : '—';
    $('axis-y').textContent = connected && pad.axes.length > 1 ? y.toFixed(2) : '—';
    $('stick-knob').style.transform = `translate(${x * 22}px, ${y * 22}px)`;
    $('plot-dot').style.left = `${50 + x * 43}%`;
    $('plot-dot').style.top = `${50 + y * 43}%`;
    $('meter-x').style.left = `${(x + 1) * 50}%`;
    $('meter-y').style.left = `${(y + 1) * 50}%`;
    visualButtons.forEach(button => {
      const i = Number(button.dataset.button);
      const active = !!(connected && pad.buttons[i] && (pad.buttons[i].pressed || pad.buttons[i].value > 0.5));
      button.classList.toggle('active', active);
      chips[i].classList.toggle('active', active);
      if (active) tested.add(i);
    });
    updateTested();
  }
  function frame() {
    if (!navigator.getGamepads) {
      $('connection-title').textContent = 'Gamepad API unavailable';
      $('connection-detail').textContent = 'Open this page in a current browser using localhost or HTTPS.';
      return;
    }
    const pads = Array.from(navigator.getGamepads()).filter(Boolean);
    updateDevices(pads);
    render(pads.find(p => p.index === selected));
    requestAnimationFrame(frame);
  }
  frame();
})();
