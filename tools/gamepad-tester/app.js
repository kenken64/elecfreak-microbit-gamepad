(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const selector = $('gamepads');
  const tested = new Set();
  const visualButtons = [...document.querySelectorAll('[data-button]')];
  const chips = [...document.querySelectorAll('[data-chip]')];
  const ledGrid = document.querySelector('.led-grid');
  const leds = Array.from({ length: 25 }, () => {
    const led = document.createElement('span');
    led.className = 'led';
    ledGrid.appendChild(led);
    return led;
  });
  // Row bitmaps and button priority match v1_display.cpp and show_live().
  const glyphs = [
    [0x04, 0x06, 0x04, 0x04, 0x0e],
    [0x0f, 0x10, 0x0e, 0x01, 0x1f],
    [0x0f, 0x10, 0x0c, 0x10, 0x0f],
    [0x0c, 0x0a, 0x09, 0x1f, 0x08],
    [0x1f, 0x01, 0x0f, 0x10, 0x0f],
    [0x0c, 0x02, 0x0f, 0x11, 0x0e],
    [0x06, 0x09, 0x0f, 0x09, 0x09],
    [0x07, 0x09, 0x07, 0x09, 0x07],
  ];
  const pressed = button => !!button && (button.pressed || button.value > 0.5);
  const axisToLed = value => Math.floor((Math.round(value * 127) + 127) * 5 / 255);
  function renderLeds(pad, x, y) {
    const key = pad ? glyphs.findIndex((_, i) => pressed(pad.buttons[i])) : -1;
    const rows = key >= 0 ? glyphs[key] : [0, 0, 0, 0, 0];
    if (key < 0) rows[axisToLed(y)] = 1 << axisToLed(x);
    leds.forEach((led, i) => led.classList.toggle('active', !!(rows[Math.floor(i / 5)] & (1 << (i % 5)))));
  }
  let selected = null;
  let previousDevices = '';
  const batteryButton = $('battery-connect');
  const batteryMessage = $('battery-message');
  const supplyService = '8c680001-c4a1-445d-b9ed-14f725715c89';
  const supplyCharacteristic = '8c680002-c4a1-445d-b9ed-14f725715c89';
  let batteryDevice = null;
  if (!navigator.bluetooth) {
    batteryButton.disabled = true;
    batteryMessage.textContent = 'Bluetooth voltage reading is unavailable in this browser.';
  }
  batteryButton.addEventListener('click', async () => {
    batteryButton.disabled = true;
    $('battery-level').textContent = 'Not measured';
    $('battery-device').textContent = '';
    batteryMessage.textContent = 'Choose your controller in the Bluetooth picker.';
    try {
      if (batteryDevice?.gatt.connected) batteryDevice.gatt.disconnect();
      batteryDevice = await navigator.bluetooth.requestDevice({
        filters: [{ services: ['battery_service'] }],
        optionalServices: [supplyService],
      });
      const device = batteryDevice;
      device.addEventListener('gattserverdisconnected', () => {
        if (batteryDevice !== device) return;
        $('battery-level').textContent = 'Not measured';
        batteryMessage.textContent = 'Bluetooth connection closed. Read again to reconnect.';
      }, { once: true });
      $('battery-device').textContent = `Bluetooth device: ${device.name || 'Unnamed device'}`;
      batteryMessage.textContent = 'Reading supply voltage…';
      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(supplyService);
      const characteristic = await service.getCharacteristic(supplyCharacteristic);
      const value = await characteristic.readValue();
      if (value.byteLength !== 2) throw new Error('Invalid voltage value');
      const millivolts = value.getUint16(0, true);
      if (!millivolts) throw new Error('No voltage sample yet');
      $('battery-level').textContent = `${(millivolts / 1000).toFixed(2)} V`;
      batteryMessage.textContent = 'Read just now. Battery charge cannot be determined from this value alone.';
    } catch (error) {
      $('battery-level').textContent = 'Not measured';
      batteryMessage.textContent = error.name === 'NotFoundError' ? 'Device or voltage service not found. Select the updated micro:bit and try again.' : 'Could not read voltage. Check device access and make sure the updated V1 firmware is installed.';
    } finally {
      batteryButton.disabled = false;
    }
  });

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
    renderLeds(pad, x, y);
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
